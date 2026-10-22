// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PRGameChannels.h"

#include "ACL.h"
#include "crypto/CryptographicHash.h"

#include <QByteArray>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace PRGameChannels {

namespace {

	// The Link plugin's context prefix: the name PR:BF2 writes into the shared memory
	const std::string CONTEXT_PREFIX = "Project Reality: BF2";

	const std::array< const char *, SQUAD_COUNT > SQUAD_KEYS = { "no",    "first", "second",  "third",  "fourth",
																 "fifth", "sixth", "seventh", "eighth", "ninth" };
	const std::array< const char *, SQUAD_COUNT > SQUAD_CHANNEL_NAMES = { "No Squad", "Squad 1", "Squad 2", "Squad 3",
																		  "Squad 4",  "Squad 5", "Squad 6", "Squad 7",
																		  "Squad 8",  "Squad 9" };

	constexpr std::array< Team, TEAM_COUNT > TEAMS = { Team::Opfor, Team::Blufor };

	const char *teamKey(Team team) { return team == Team::Opfor ? "opfor" : "blufor"; }

	const char *teamChannelName(Team team) { return team == Team::Opfor ? "Team 1" : "Team 2"; }

	std::string squadGroup(unsigned int squad) { return std::string("bf2_") + SQUAD_KEYS[squad] + "_squad"; }

	std::string squadLeaderGroup(unsigned int squad) {
		return std::string("bf2_") + SQUAD_KEYS[squad] + "_squad_leader";
	}

	std::string gameGroup(const std::string &code) { return "bf2_" + code + "_game"; }

	std::string adminGroup(const std::string &code) { return "bf2_" + code + "_admin"; }

	const std::string TEAM_GROUP         = "bf2_team";
	const std::string COMMANDER_GROUP    = "bf2_commander";
	const std::string SQUAD_LEADER_GROUP = "bf2_squad_leader";

	nlohmann::json parseObject(const std::string &text) {
		nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
		if (json.is_discarded() || !json.is_object()) {
			return nlohmann::json();
		}
		return json;
	}

	std::string credentialToString(const nlohmann::json &identity, const char *key) {
		auto it = identity.find(key);
		if (it == identity.end()) {
			return {};
		}
		if (it->is_string()) {
			return it->get< std::string >();
		}
		if (it->is_number_integer()) {
			return it->dump();
		}
		return {};
	}

	unsigned int requireChannelID(const nlohmann::json &object, const char *key) {
		auto it = object.find(key);
		if (it == object.end() || !it->is_number_unsigned()) {
			throw std::invalid_argument(std::string("missing or invalid channel ID \"") + key + "\"");
		}
		return it->get< unsigned int >();
	}

	template< typename T > T require(const nlohmann::json &object, const char *key) {
		auto it = object.find(key);
		if (it == object.end()) {
			throw std::invalid_argument(std::string("missing \"") + key + "\"");
		}
		try {
			return it->get< T >();
		} catch (const nlohmann::json::exception &) {
			throw std::invalid_argument(std::string("invalid \"") + key + "\"");
		}
	}

} // namespace

bool isLinked(const std::string &context) {
	return context.compare(0, CONTEXT_PREFIX.size() + 1, CONTEXT_PREFIX + '\0') == 0 || context == CONTEXT_PREFIX;
}

std::optional< std::string > parseContextIPPort(const std::string &context) {
	if (!isLinked(context) || context.size() <= CONTEXT_PREFIX.size()) {
		return std::nullopt;
	}

	const nlohmann::json json = parseObject(context.substr(CONTEXT_PREFIX.size() + 1));
	auto it                   = json.find("ipport");
	if (!json.is_object() || it == json.end() || !it->is_string()) {
		return std::nullopt;
	}

	return it->get< std::string >();
}

std::optional< Identity > parseIdentity(const std::string &identityText) {
	const nlohmann::json json = parseObject(identityText);
	if (!json.is_object()) {
		return std::nullopt;
	}

	auto team        = json.find("team");
	auto squad       = json.find("squad");
	auto squadLeader = json.find("squad_leader");
	auto commander   = json.find("commander");
	if (team == json.end() || squad == json.end() || squadLeader == json.end() || commander == json.end()) {
		return std::nullopt;
	}
	if (!team->is_string() || !squad->is_number_integer() || !squadLeader->is_boolean() || !commander->is_boolean()) {
		return std::nullopt;
	}

	Identity identity;

	const std::string teamName = team->get< std::string >();
	if (teamName == teamKey(Team::Opfor)) {
		identity.team = Team::Opfor;
	} else if (teamName == teamKey(Team::Blufor)) {
		identity.team = Team::Blufor;
	} else {
		return std::nullopt;
	}

	const long long squadNumber = squad->get< long long >();
	if (squadNumber < 0 || squadNumber >= static_cast< long long >(SQUAD_COUNT)) {
		return std::nullopt;
	}
	identity.squad = static_cast< unsigned int >(squadNumber);

	identity.squadLeader = squadLeader->get< bool >();
	identity.commander   = commander->get< bool >();
	identity.hash        = credentialToString(json, "hash");
	identity.pass        = credentialToString(json, "pass");

	return identity;
}

std::string computePass(long long minutesSinceEpoch, const std::string &hash, const std::string &secret) {
	const std::string input = std::to_string(minutesSinceEpoch) + hash + secret;
	const QByteArray digest = CryptographicHash::hash(QByteArray::fromStdString(input), CryptographicHash::Sha1);

	// The game server takes the first 4 bytes as a little-endian integer and keeps the lowest 23 bits
	const unsigned int value = (static_cast< unsigned char >(digest.at(0))
								| (static_cast< unsigned int >(static_cast< unsigned char >(digest.at(1))) << 8)
								| (static_cast< unsigned int >(static_cast< unsigned char >(digest.at(2))) << 16))
							   & 0x7FFFFF;

	return std::to_string(value);
}

bool verifyPass(const std::string &hash, const std::string &pass, const std::string &secret,
				long long currentMinutesSinceEpoch) {
	if (pass.empty()) {
		return false;
	}

	for (long long offset = -1; offset <= 1; ++offset) {
		if (computePass(currentMinutesSinceEpoch + offset, hash, secret) == pass) {
			return true;
		}
	}

	return false;
}

const Game *findGameByIPPort(const std::vector< Game > &games, const std::string &ipPort) {
	for (const Game &game : games) {
		if (std::find(game.ipPorts.begin(), game.ipPorts.end(), ipPort) != game.ipPorts.end()) {
			return &game;
		}
	}
	return nullptr;
}

const Game *findGameByCode(const std::vector< Game > &games, const std::string &code) {
	for (const Game &game : games) {
		if (game.code == code) {
			return &game;
		}
	}
	return nullptr;
}

Assignment assignmentFor(const Game &game, const Identity &identity) {
	const TeamChannels &channels = game.teams[static_cast< std::size_t >(identity.team)];

	Assignment assignment;
	assignment.gameGroups    = { gameGroup(game.code) };
	assignment.teamChannel   = channels.team;
	assignment.teamGroups    = { TEAM_GROUP, squadGroup(identity.squad) };
	assignment.targetChannel = channels.squads[identity.squad];

	if (identity.squadLeader) {
		assignment.teamGroups.push_back(squadLeaderGroup(identity.squad));
		assignment.teamGroups.push_back(SQUAD_LEADER_GROUP);
	}
	if (identity.commander) {
		assignment.teamGroups.push_back(COMMANDER_GROUP);
		if (!identity.squadLeader) {
			assignment.teamGroups.push_back(SQUAD_LEADER_GROUP);
		}
		assignment.targetChannel = channels.commander;
	}

	return assignment;
}

bool isValidCode(const std::string &code) {
	if (code.empty() || code.size() > 32) {
		return false;
	}
	return std::all_of(code.begin(), code.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	});
}

bool isValidIPPort(const std::string &ipPort) {
	const std::size_t colon = ipPort.rfind(':');
	if (colon == std::string::npos || colon == 0 || ipPort.size() > 64) {
		return false;
	}

	const std::string host = ipPort.substr(0, colon);
	const std::string port = ipPort.substr(colon + 1);

	if (std::any_of(host.begin(), host.end(), [](char c) { return c == ',' || c <= ' ' || c > '~'; })) {
		return false;
	}
	if (port.empty() || port.size() > 5
		|| !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; })) {
		return false;
	}

	const int portNumber = std::stoi(port);
	return portNumber > 0 && portNumber <= 65535;
}

std::vector< Game > gamesFromJSON(const std::string &text) {
	if (text.empty()) {
		return {};
	}

	const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
	if (json.is_discarded() || !json.is_array()) {
		throw std::invalid_argument("not a JSON list");
	}

	std::vector< Game > games;
	for (const nlohmann::json &entry : json) {
		if (!entry.is_object()) {
			throw std::invalid_argument("game entry is not an object");
		}

		Game game;
		game.code      = require< std::string >(entry, "code");
		game.name      = require< std::string >(entry, "name");
		game.ipPorts   = require< std::vector< std::string > >(entry, "ipPorts");
		game.secret    = require< std::string >(entry, "secret");
		game.verify    = require< bool >(entry, "verify");
		game.linkTeams = require< bool >(entry, "linkTeams");

		const nlohmann::json channels = require< nlohmann::json >(entry, "channels");
		if (!channels.is_object()) {
			throw std::invalid_argument("\"channels\" is not an object");
		}
		game.gameChannel = requireChannelID(channels, "game");

		for (Team team : TEAMS) {
			const nlohmann::json teamJSON = require< nlohmann::json >(channels, teamKey(team));
			if (!teamJSON.is_object()) {
				throw std::invalid_argument(std::string("\"") + teamKey(team) + "\" is not an object");
			}

			TeamChannels &teamChannels = game.teams[static_cast< std::size_t >(team)];
			teamChannels.team          = requireChannelID(teamJSON, "team");
			teamChannels.commander     = requireChannelID(teamJSON, "commander");

			const nlohmann::json squads = require< nlohmann::json >(teamJSON, "squads");
			if (!squads.is_array() || squads.size() != SQUAD_COUNT) {
				throw std::invalid_argument("\"squads\" must be a list of " + std::to_string(SQUAD_COUNT) + " IDs");
			}
			for (std::size_t i = 0; i < SQUAD_COUNT; ++i) {
				if (!squads[i].is_number_unsigned()) {
					throw std::invalid_argument("invalid squad channel ID");
				}
				teamChannels.squads[i] = squads[i].get< unsigned int >();
			}
		}

		games.push_back(std::move(game));
	}

	return games;
}

std::string gamesToJSON(const std::vector< Game > &games) {
	nlohmann::json json = nlohmann::json::array();

	for (const Game &game : games) {
		nlohmann::json channels;
		channels["game"] = game.gameChannel;
		for (Team team : TEAMS) {
			const TeamChannels &teamChannels = game.teams[static_cast< std::size_t >(team)];
			channels[teamKey(team)]          = { { "team", teamChannels.team },
                                        { "commander", teamChannels.commander },
                                        { "squads", teamChannels.squads } };
		}

		json.push_back({ { "code", game.code },
						 { "name", game.name },
						 { "ipPorts", game.ipPorts },
						 { "secret", game.secret },
						 { "verify", game.verify },
						 { "linkTeams", game.linkTeams },
						 { "channels", channels } });
	}

	return json.dump();
}

SetupSpec setupSpec() {
	const unsigned int speakWhisper = ChanACL::Speak | ChanACL::Whisper;

	SetupSpec spec;

	spec.rootACLs = {
		{ "admin", true, true, ChanACL::Write, 0 },
		{ "admin", true, true, ChanACL::Register, 0 },
		{ "all", true, false, 0, speakWhisper },
		{ "all", true, true, 0, ChanACL::SelfRegister },
	};

	spec.rootGroups = { TEAM_GROUP, COMMANDER_GROUP, SQUAD_LEADER_GROUP };
	for (unsigned int squad = 0; squad < SQUAD_COUNT; ++squad) {
		spec.rootGroups.push_back(squadGroup(squad));
	}

	spec.channels = {
		{ ChannelSpec::Role::Base,
		  "PR BF2 Game Servers",
		  -1,
		  1,
		  false,
		  { { "all", true, true, 0, speakWhisper } },
		  {},
		  std::nullopt,
		  0 },
		{ ChannelSpec::Role::Lobby,
		  "Lobby",
		  -1,
		  2,
		  false,
		  { { "all", true, true, 0, speakWhisper } },
		  {},
		  std::nullopt,
		  0 },
	};

	return spec;
}

std::vector< ChannelSpec > gameTreeSpec(const std::string &code, const std::string &name, bool linkTeams) {
	const unsigned int traverseWhisper = ChanACL::Traverse | ChanACL::Whisper;
	const unsigned int speakWhisper    = ChanACL::Speak | ChanACL::Whisper;
	const unsigned int speakText       = ChanACL::Speak | ChanACL::TextMessage;
	const unsigned int adminTeam       = ChanACL::Traverse | ChanACL::MuteDeafen | ChanACL::Move;
	const unsigned int adminGame       = ChanACL::Traverse | ChanACL::MuteDeafen | ChanACL::Move | ChanACL::MakeChannel;
	const unsigned int teamOnly        = ChanACL::Enter | ChanACL::Traverse | ChanACL::Whisper | ChanACL::Speak
								  | ChanACL::TextMessage | ChanACL::MakeChannel;
	const std::string game        = "~" + gameGroup(code);
	const std::string admin       = "~" + adminGroup(code);
	const std::string team        = "~" + TEAM_GROUP;
	const std::string commander   = "~" + COMMANDER_GROUP;
	const std::string squadLeader = "~" + SQUAD_LEADER_GROUP;

	std::vector< ChannelSpec > spec;

	spec.push_back({ ChannelSpec::Role::Game,
					 name,
					 -1,
					 0,
					 false,
					 { { admin, true, true, adminGame, 0 }, { "all", true, false, 0, speakWhisper } },
					 { gameGroup(code), adminGroup(code) },
					 std::nullopt,
					 0 });

	for (Team currentTeam : TEAMS) {
		const int teamIndex = static_cast< int >(spec.size());

		spec.push_back({ ChannelSpec::Role::Team,
						 teamChannelName(currentTeam),
						 0,
						 0,
						 linkTeams,
						 { { "all", true, true, 0, teamOnly },
						   { game, true, true, speakText, 0 },
						   { game, true, false, traverseWhisper, 0 },
						   { team, true, false, traverseWhisper, 0 },
						   { admin, true, true, adminTeam, 0 } },
						 {},
						 currentTeam,
						 0 });

		spec.push_back(
			{ ChannelSpec::Role::Commander,
			  "Commander",
			  teamIndex,
			  -1,
			  true,
			  { { commander, true, false, traverseWhisper, 0 }, { squadLeader, true, false, ChanACL::Whisper, 0 } },
			  {},
			  currentTeam,
			  0 });

		for (unsigned int squad = 0; squad < SQUAD_COUNT; ++squad) {
			spec.push_back({ ChannelSpec::Role::Squad,
							 SQUAD_CHANNEL_NAMES[squad],
							 teamIndex,
							 0,
							 true,
							 { { "~" + squadGroup(squad), true, false, traverseWhisper, 0 },
							   { commander, true, false, traverseWhisper, 0 },
							   { squadLeader, true, false, ChanACL::Whisper, 0 } },
							 {},
							 currentTeam,
							 squad });
		}
	}

	return spec;
}

} // namespace PRGameChannels
