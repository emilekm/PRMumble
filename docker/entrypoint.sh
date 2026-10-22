#!/usr/bin/env bash
# Adapted from the entrypoint of the official Mumble Docker image (https://github.com/mumble-voip/mumble-docker).
set -e

log() {
	echo "[prmurmur] $*"
}

PUID=${PUID:-10000}
PGID=${PGID:-10000}
MUMBLE_CHOWN_DATA=${MUMBLE_CHOWN_DATA:-true}

readonly DATA_DIR="/data"
readonly BARE_BONES_CONFIG_FILE="/etc/prmurmur/bare_config.ini"
readonly CONFIG_REGEX="^(\;|\#)?\ *([a-zA-Z_0-9]+)=.*"
CONFIG_FILE="${DATA_DIR}/prmurmur.ini"

readonly SENSITIVE_CONFIGS=(
	"dbPassword"
	"serverpassword"
	"registerpassword"
	"sslPassPhrase"
)

if [[ "$1" = "bash" || "$1" = "sh" ]]; then
	log "Dropping into interactive shell"
	exec "${@}"
fi

declare -a as_server_user=()
if [[ "$(id -u)" = "0" ]] && [[ "${PUID}" != "0" ]]; then
	as_server_user=(setpriv --reuid="$PUID" --regid="$PGID" --clear-groups)
fi

# Compile list of configuration options from the bare-bones config
readarray -t existing_config_options < <(sed -En "s/$CONFIG_REGEX/\2/p" "$BARE_BONES_CONFIG_FILE")

declare -a used_configs

normalize_name() {
	local uppercase="${1^^}"
	echo "${uppercase//_/}"
}

# Create an associative array for faster config option lookup
declare -A option_for

for config in "${existing_config_options[@]}"; do
	option_for["$(normalize_name "$config")"]="$config"
done

array_contains() {
	local array_expansion="$1[@]" seeking="$2"
	for element in "${!array_expansion}"; do
		[[ "$element" = "$seeking" ]] && return 0
	done
	return 1
}

set_config() {
	local config_name="$1" config_value="$2" is_default="$3"

	[[ "$is_default" = true ]] && array_contains "used_configs" "$config_name" && \
		return 0 # Don't use default value if the user already set one!

	if array_contains "SENSITIVE_CONFIGS" "$config_name"; then
		log "Setting config \"$config_name\" to: *********"
	else
		log "Setting config \"$config_name\" to: '$config_value'"
	fi
	used_configs+=("$config_name")

	echo "${config_name}=${config_value}" >> "$CONFIG_FILE"
}

set_config_from() {
	local var="$1" value="$2" source="$3"
	local config_option="${option_for[$(normalize_name "$var")]}"

	if [[ -n "$config_option" ]]; then
		set_config "$config_option" "$value"
	elif [[ "$MUMBLE_ACCEPT_UNKNOWN_SETTINGS" = true ]]; then
		log "[WARNING]: Unable to find config corresponding to $source. Make sure that it is correctly spelled, using it as-is"
		set_config "$var" "$value"
	else
		>&2 log "[ERROR]: Unable to find config corresponding to $source"
		exit 1
	fi
}

if [[ -f "$MUMBLE_CUSTOM_CONFIG_FILE" ]]; then
	log "Using manually specified config file at $MUMBLE_CUSTOM_CONFIG_FILE"
	log "All MUMBLE_CONFIG variables will be ignored"
	CONFIG_FILE="$MUMBLE_CUSTOM_CONFIG_FILE"
else
	# Ensures the config file is empty, starting from a clean slate
	echo -e "# Config file automatically generated from the MUMBLE_CONFIG_* environment variables" > "${CONFIG_FILE}"
	echo -e "# or secrets in /run/secrets/MUMBLE_CONFIG_* files\n" >> "${CONFIG_FILE}"

	while IFS='=' read -d '' -r var value; do
		set_config_from "$var" "$value" "variable \"$var\""
	done < <( printenv --null | sed -zn 's/^MUMBLE_CONFIG_//p' )
	# ^ Feeding it in like this, prevents the creation of a subshell for the while-loop

	while read -r var; do
		secret_file="/run/secrets/MUMBLE_CONFIG_$var"
		set_config_from "$var" "$(cat "$secret_file")" "container secret \"$secret_file\""
	done < <( ls /run/secrets 2> /dev/null | sed -n 's/^MUMBLE_CONFIG_//p' )

	set_config "database" "${DATA_DIR}/prmurmur.sqlite" true
	# Log to stdout only
	set_config "logfile" "" true
	set_config "logsys" "" true

	if ! array_contains "used_configs" "welcometextfile"; then
		set_config "welcometext" "\"<br />Welcome to this <b>Project Reality</b> Mumble server.<br />Enjoy your stay!<br />\"" true
	fi

	set_config "port" 64740 true
	set_config "users" 100 true
fi

# Take ownership before the server user creates any files in /data.
# If the container runs as an unprivileged user, its owner handles the ownership of /data.
if [[ "$(id -u)" = "0" ]] && [[ "${PUID}" != "0" ]] && [[ "${MUMBLE_CHOWN_DATA}" = true ]]; then
	chown -R "${PUID}:${PGID}" "$DATA_DIR"
fi

declare -a server_invocation=("${as_server_user[@]}" /usr/local/bin/prmurmurd --ini "$CONFIG_FILE")

[[ "$MUMBLE_VERBOSE" = true ]] && server_invocation+=(--verbose)

# Arguments such as --pr-list-games run as a one-off command instead of the server
if [[ "$#" -gt 0 ]]; then
	exec "${server_invocation[@]}" "$@"
fi

if [[ -f /run/secrets/MUMBLE_SUPERUSER_PASSWORD ]]; then
	MUMBLE_SUPERUSER_PASSWORD="$(cat /run/secrets/MUMBLE_SUPERUSER_PASSWORD)"
	log "Read superuser password from container secret"
fi

if [[ -n "${MUMBLE_SUPERUSER_PASSWORD}" ]]; then
	"${server_invocation[@]}" --set-su-pw "$MUMBLE_SUPERUSER_PASSWORD"
	log "Successfully configured superuser password"
fi

if [[ -f /run/secrets/PR_GAME_SECRET ]]; then
	PR_GAME_SECRET="$(cat /run/secrets/PR_GAME_SECRET)"
	log "Read PR game secret from container secret"
fi

if [[ -n "$PR_GAME_CODE" || -n "$PR_GAME_NAME" || -n "$PR_GAME_IPPORT" ]]; then
	if [[ -z "$PR_GAME_CODE" || -z "$PR_GAME_NAME" || -z "$PR_GAME_IPPORT" ]]; then
		>&2 log "[ERROR]: PR_GAME_CODE, PR_GAME_NAME and PR_GAME_IPPORT must be set together"
		exit 1
	fi

	declare -a add_game=(--pr-add-game "$PR_GAME_CODE" --pr-name "$PR_GAME_NAME" --pr-ipport "$PR_GAME_IPPORT")
	[[ -n "$PR_GAME_SECRET" ]] && add_game+=(--pr-secret "$PR_GAME_SECRET")

	"${server_invocation[@]}" "${add_game[@]}"
fi

log "Running prmurmurd as uid=$(id -u) gid=$(id -g)${as_server_user:+, dropping to uid=${PUID} gid=${PGID}}"
log "\"${DATA_DIR}\" has the following permissions set:"
log "  $( stat ${DATA_DIR} --printf='%A, owner: \"%U\" (UID: %u), group: \"%G\" (GID: %g)' )"
log "Starting..."

exec "${server_invocation[@]}" --foreground
