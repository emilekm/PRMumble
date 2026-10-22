# Project Reality Murmur Docker image

The image runs `prmurmurd` and registers one PR:BF2 game server on start. Configuration works like in the
[official Mumble Docker image](https://github.com/mumble-voip/mumble-docker), whose entrypoint this one is adapted
from.

## Building

From the repository root, with the submodules initialized:

```
git submodule update --init --recursive
docker build -f docker/Dockerfile -t prmurmur .
```

The image is built from the working tree, including uncommitted changes. The CMake build directory is kept in a
BuildKit cache mount, so rebuilds after small changes only recompile what changed.

## Running

```yaml
services:
    prmurmur:
        image: prmurmur
        restart: on-failure
        ports:
            - 64740:64740
            - 64740:64740/udp
        volumes:
            - ./data:/data
        environment:
            PR_GAME_CODE: awesome
            PR_GAME_NAME: Awesome PR Server
            PR_GAME_IPPORT: 203.0.113.10:16567
            PR_GAME_SECRET: <secret from realityconfig_admin.py>
            MUMBLE_SUPERUSER_PASSWORD: <password>
```

`/data` holds the database (`prmurmur.sqlite`, which also stores the server's TLS certificate) and the generated
`prmurmur.ini`. The server logs to stdout only.

## Configuration

| Environment variable | Description |
|---|---|
| `PR_GAME_CODE` | Code of the game server to register (`--pr-add-game`), e.g. `awesome`. |
| `PR_GAME_NAME` | Name of the game's channel (`--pr-name`). |
| `PR_GAME_IPPORT` | `<ip>:<port>[,<ip>:<port>...]` the game server reports to players (`--pr-ipport`). |
| `PR_GAME_SECRET` | Identity secret shared with the game server (`--pr-secret`). If not set, a secret is generated and printed to the log once, on the first start. |
| `MUMBLE_CONFIG_<name>` | Sets the server setting `<name>`, e.g. `MUMBLE_CONFIG_USERS=200`. `<name>` is case-insensitive and may contain underscores (`MUMBLE_CONFIG_SERVER_PASSWORD`). See [mumble-server.ini](../auxiliary_files/mumble-server.ini) for the available settings. |
| `MUMBLE_SUPERUSER_PASSWORD` | Sets the SuperUser password on every start. |
| `MUMBLE_CUSTOM_CONFIG_FILE` | Path to an ini file to use instead. All `MUMBLE_CONFIG_*` variables are then ignored. |
| `MUMBLE_ACCEPT_UNKNOWN_SETTINGS` | Set to `true` to pass `MUMBLE_CONFIG_*` settings that the image doesn't know through as-is, instead of failing. |
| `MUMBLE_VERBOSE` | Set to `true` for verbose logging. |
| `PUID`, `PGID` | User and group the server runs as (default `10000`). |
| `MUMBLE_CHOWN_DATA` | Set to `false` to not take ownership of `/data` on start. |

`PR_GAME_CODE`, `PR_GAME_NAME` and `PR_GAME_IPPORT` must be set together. Without them, no game is registered.
`PR_GAME_SECRET`, `MUMBLE_SUPERUSER_PASSWORD` and `MUMBLE_CONFIG_*` can also be given as container secrets of the
same name in `/run/secrets/`.

Defaults that differ from a plain `prmurmurd`: port `64740`, no log file and no syslog.

## Changing the registered game

Registering the game is skipped if it's already registered with the same settings. If `PR_GAME_*` change, the
container fails to start with an error. Remove the game while the server is stopped, then start the container
again with the new settings:

```
docker compose stop prmurmur
docker compose run --rm prmurmur --pr-remove-game awesome
docker compose up -d prmurmur
```

Removing a game deletes its channels, including their ACLs and the members of its `bf2_<code>_admin` group.
Arguments given to the container run `prmurmurd` with them once instead of starting the server, e.g.
`--pr-list-games`. See [PRServerSetup.md](../docs/PRServerSetup.md) for the `--pr-*` commands.
