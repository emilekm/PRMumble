// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MURMUR_PRGAMECHANNELS_H_
#define MUMBLE_MURMUR_PRGAMECHANNELS_H_

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/// Server-independent parts of sorting Project Reality: BF2 players into their game's team and squad channels:
/// parsing what the game reports through the Link plugin, verifying it, deciding on groups and channels, and
/// describing the channel tree and permissions a game needs.
namespace PRGameChannels {

enum class Team { Opfor, Blufor };

constexpr std::size_t TEAM_COUNT = 2;
/// Squad 0 is "No Squad"
constexpr std::size_t SQUAD_COUNT = 10;

constexpr const char *LINKED_GROUP = "bf2_linked";
/// The virtual server config entry holding the registered games (see gamesToJSON)
constexpr const char *GAMES_CONFIG_KEY = "prgames";

struct Identity {
	Team team;
	unsigned int squad;
	bool squadLeader;
	bool commander;
	std::string hash;
	std::string pass;
};

struct TeamChannels {
	unsigned int team      = 0;
	unsigned int commander = 0;
	std::array< unsigned int, SQUAD_COUNT > squads{};
};

struct Game {
	std::string code;
	std::string name;
	std::vector< std::string > ipPorts;
	/// Empty if and only if verify is false
	std::string secret;
	bool verify    = true;
	bool linkTeams = false;

	unsigned int gameChannel = 0;
	/// Indexed by Team
	std::array< TeamChannels, TEAM_COUNT > teams{};
};

/// The groups to put a sorted player in and the channel to move them to
struct Assignment {
	/// Temporary groups on Game::gameChannel
	std::vector< std::string > gameGroups;
	unsigned int teamChannel;
	/// Temporary groups on teamChannel
	std::vector< std::string > teamGroups;
	unsigned int targetChannel;
};

/// Whether the plugin context comes from Project Reality: BF2
bool isLinked(const std::string &context);
/// The game server address in a Project Reality: BF2 plugin context
std::optional< std::string > parseContextIPPort(const std::string &context);
std::optional< Identity > parseIdentity(const std::string &identity);

/// The one-time password the game server derives from the player's hash for the given minute since the epoch
std::string computePass(long long minutesSinceEpoch, const std::string &hash, const std::string &secret);
/// Accepts the passes of the previous, current and next minute to allow for clock skew
bool verifyPass(const std::string &hash, const std::string &pass, const std::string &secret,
				long long currentMinutesSinceEpoch);

const Game *findGameByIPPort(const std::vector< Game > &games, const std::string &ipPort);
const Game *findGameByCode(const std::vector< Game > &games, const std::string &code);

Assignment assignmentFor(const Game &game, const Identity &identity);

bool isValidCode(const std::string &code);
bool isValidIPPort(const std::string &ipPort);

/// @throws std::invalid_argument if json isn't a valid list of games
std::vector< Game > gamesFromJSON(const std::string &json);
std::string gamesToJSON(const std::vector< Game > &games);

struct ACLSpec {
	/// A group specification as used by ChanACL::qsGroup, e.g. "all" or "~bf2_team"
	std::string group;
	bool applyHere;
	bool applySubs;
	unsigned int allow;
	unsigned int deny;
};

struct ChannelSpec {
	enum class Role { Base, Lobby, Game, Team, Commander, Squad };

	Role role;
	std::string name;
	/// Index of the parent in the list of specs, or -1 for the channel the list is created under
	int parent;
	int position;
	bool linkedToParent;
	std::vector< ACLSpec > acls;
	/// Persistent (empty) groups to define on this channel, so ACLs can refer to them
	std::vector< std::string > groups;
	/// Only set for Team, Commander and Squad
	std::optional< Team > team;
	/// Only set for Squad
	unsigned int squad = 0;
};

/// What gets set up once per virtual server before the first game is added
struct SetupSpec {
	/// Replaces the root channel's ACL
	std::vector< ACLSpec > rootACLs;
	/// Persistent groups shared by all games, defined on the root channel so removing a game never removes them
	std::vector< std::string > rootGroups;
	/// Created under the root channel
	std::vector< ChannelSpec > channels;
};

SetupSpec setupSpec();
/// The channel tree of one game, created under the channel with role Base
std::vector< ChannelSpec > gameTreeSpec(const std::string &code, const std::string &name, bool linkTeams);

} // namespace PRGameChannels

#endif // MUMBLE_MURMUR_PRGAMECHANNELS_H_
