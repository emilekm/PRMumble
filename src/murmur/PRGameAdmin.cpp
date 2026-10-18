// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PRGameAdmin.h"

#include "DBWrapper.h"
#include "MumbleConstants.h"
#include "PRGameChannels.h"
#include "PasswordGenerator.h"
#include "database/ConnectionParameter.h"
#include "database/TransactionHolder.h"
#include "murmur/database/ACLCompat.h"
#include "murmur/database/ACLTable.h"
#include "murmur/database/ChannelLinkTable.h"
#include "murmur/database/ChannelProperty.h"
#include "murmur/database/ChannelPropertyTable.h"
#include "murmur/database/ChannelTable.h"
#include "murmur/database/ConfigTable.h"
#include "murmur/database/DBAcl.h"
#include "murmur/database/DBChannel.h"
#include "murmur/database/DBChannelLink.h"
#include "murmur/database/DBGroup.h"
#include "murmur/database/GroupTable.h"
#include "murmur/database/ServerDatabase.h"

#include <CLI/CLI.hpp>

#include <QtCore/QtGlobal>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace mdb  = ::mumble::db;
namespace msdb = ::mumble::server::db;

namespace PRGameAdmin {

namespace {

	/// Set once the one-time setup has run; holds the ID of the channel games are created under
	constexpr const char *SETUP_CONFIG_KEY = "prsetup";
	constexpr const char *CLI_SECTION      = "Project Reality";
	constexpr int GENERATED_SECRET_LENGTH  = 20;

	unsigned int resolveServer(const CLIOptions &options, const mdb::ConnectionParameter &connectionParameter,
							   bool createIfMissing) {
		DBWrapper wrapper(connectionParameter);

		if (options.server) {
			if (!wrapper.serverExists(*options.server)) {
				throw std::runtime_error("Virtual server " + std::to_string(*options.server) + " does not exist");
			}
			return *options.server;
		}

		const std::vector< unsigned int > servers = wrapper.getAllServers();
		if (servers.size() == 1) {
			return servers.front();
		}
		if (!servers.empty()) {
			throw std::runtime_error("The database contains several virtual servers, select one with --pr-server");
		}
		if (!createIfMissing) {
			throw std::runtime_error("The database contains no virtual server");
		}

		// Same as Meta::bootAll does on the first start
		const unsigned int serverID = wrapper.addServer();
		wrapper.setServerBootProperty(serverID, true);
		qInfo("Created new server default instance");

		return serverID;
	}

	std::vector< PRGameChannels::Game > loadGames(msdb::ServerDatabase &db, unsigned int serverID) {
		try {
			return PRGameChannels::gamesFromJSON(
				db.getConfigTable().getConfig(serverID, PRGameChannels::GAMES_CONFIG_KEY));
		} catch (const std::invalid_argument &e) {
			throw std::runtime_error(std::string("The stored list of PR games is invalid: ") + e.what());
		}
	}

	void writeACLs(msdb::ServerDatabase &db, unsigned int serverID, unsigned int channelID,
				   const std::vector< PRGameChannels::ACLSpec > &acls) {
		// Same starting priority as DBWrapper::updateChannelData
		unsigned int priority = 5;

		for (const PRGameChannels::ACLSpec &spec : acls) {
			msdb::LegacyGroupData group = msdb::parseLegacyGroupSpecification(spec.group);

			msdb::DBAcl acl;
			acl.serverID              = serverID;
			acl.channelID             = channelID;
			acl.priority              = priority++;
			acl.applyInCurrentChannel = spec.applyHere;
			acl.applyInSubChannels    = spec.applySubs;
			acl.grantedPrivilegeFlags = spec.allow;
			acl.revokedPrivilegeFlags = spec.deny;
			acl.groupModifiers        = group.modifiers;
			acl.affectedMetaGroup     = msdb::parseMetaGroup(group.name);

			if (!acl.affectedMetaGroup) {
				acl.affectedGroupID = db.getGroupTable().findGroupID(serverID, group.name);
				if (!acl.affectedGroupID) {
					throw std::runtime_error("ACL refers to the undefined group \"" + group.name + "\"");
				}
			}

			db.getACLTable().addACL(acl);
		}
	}

	void addGroup(msdb::ServerDatabase &db, unsigned int serverID, unsigned int channelID, const std::string &name) {
		msdb::DBGroup group;
		group.serverID  = serverID;
		group.groupID   = db.getGroupTable().getFreeGroupID(serverID);
		group.channelID = channelID;
		group.name      = name;

		db.getGroupTable().addGroup(group);
	}

	unsigned int createChannel(msdb::ServerDatabase &db, unsigned int serverID, unsigned int parentID,
							   const PRGameChannels::ChannelSpec &spec) {
		msdb::DBChannel channel;
		channel.serverID   = serverID;
		channel.channelID  = db.getChannelTable().getFreeChannelID(serverID);
		channel.parentID   = parentID;
		channel.name       = spec.name;
		channel.inheritACL = true;

		db.getChannelTable().addChannel(channel);

		db.getChannelPropertyTable().setProperty(serverID, channel.channelID, msdb::ChannelProperty::Position,
												 std::to_string(spec.position));
		db.getChannelPropertyTable().setProperty(serverID, channel.channelID, msdb::ChannelProperty::MaxUsers, "0");

		for (const std::string &group : spec.groups) {
			addGroup(db, serverID, channel.channelID, group);
		}

		writeACLs(db, serverID, channel.channelID, spec.acls);

		if (spec.linkedToParent) {
			db.getChannelLinkTable().addLink(msdb::DBChannelLink(serverID, parentID, channel.channelID));
		}

		return channel.channelID;
	}

	unsigned int ensureSetup(msdb::ServerDatabase &db, unsigned int serverID) {
		const std::string marker = db.getConfigTable().getConfig(serverID, SETUP_CONFIG_KEY);
		if (!marker.empty()) {
			const unsigned int baseChannel = static_cast< unsigned int >(std::stoul(marker));
			if (!db.getChannelTable().channelExists(serverID, baseChannel)) {
				throw std::runtime_error("The channel PR games are created under (" + marker + ") no longer exists");
			}
			return baseChannel;
		}

		const PRGameChannels::SetupSpec spec = PRGameChannels::setupSpec();

		for (const std::string &group : spec.rootGroups) {
			if (!db.getGroupTable().findGroupID(serverID, group)) {
				addGroup(db, serverID, Mumble::ROOT_CHANNEL_ID, group);
			}
		}
		if (!db.getGroupTable().findGroupID(serverID, "admin")) {
			addGroup(db, serverID, Mumble::ROOT_CHANNEL_ID, "admin");
		}

		db.getACLTable().clearACLs(serverID, Mumble::ROOT_CHANNEL_ID);
		writeACLs(db, serverID, Mumble::ROOT_CHANNEL_ID, spec.rootACLs);

		std::optional< unsigned int > baseChannel;
		for (const PRGameChannels::ChannelSpec &channel : spec.channels) {
			const unsigned int channelID = createChannel(db, serverID, Mumble::ROOT_CHANNEL_ID, channel);
			if (channel.role == PRGameChannels::ChannelSpec::Role::Base) {
				baseChannel = channelID;
			}
		}

		db.getConfigTable().setConfig(serverID, SETUP_CONFIG_KEY, std::to_string(baseChannel.value()));
		qInfo("Set up the PR channel structure on server %u", serverID);

		return baseChannel.value();
	}

	PRGameChannels::Game createGameTree(msdb::ServerDatabase &db, unsigned int serverID, unsigned int baseChannel,
										const CLIOptions &options) {
		using Role = PRGameChannels::ChannelSpec::Role;

		const std::vector< PRGameChannels::ChannelSpec > spec =
			PRGameChannels::gameTreeSpec(*options.addGame, *options.name, options.linkTeams);

		PRGameChannels::Game game;
		std::vector< unsigned int > channelIDs;

		for (const PRGameChannels::ChannelSpec &channel : spec) {
			const unsigned int parentID =
				channel.parent < 0 ? baseChannel : channelIDs.at(static_cast< std::size_t >(channel.parent));
			const unsigned int channelID = createChannel(db, serverID, parentID, channel);
			channelIDs.push_back(channelID);

			if (channel.role == Role::Game) {
				game.gameChannel = channelID;
				continue;
			}

			PRGameChannels::TeamChannels &team = game.teams[static_cast< std::size_t >(channel.team.value())];
			switch (channel.role) {
				case Role::Team:
					team.team = channelID;
					break;
				case Role::Commander:
					team.commander = channelID;
					break;
				case Role::Squad:
					team.squads[channel.squad] = channelID;
					break;
				default:
					break;
			}
		}

		return game;
	}

	std::string joined(const std::vector< std::string > &values) {
		std::string result;
		for (const std::string &value : values) {
			result += (result.empty() ? "" : ",") + value;
		}
		return result;
	}

	int addGame(msdb::ServerDatabase &db, unsigned int serverID, const CLIOptions &options) {
		const std::string &code = *options.addGame;

		if (!PRGameChannels::isValidCode(code)) {
			throw std::runtime_error("Invalid game code \"" + code + "\" (use up to 32 of a-z, A-Z, 0-9, _ and -)");
		}
		if (!options.name || options.name->empty()) {
			throw std::runtime_error("--pr-add-game requires --pr-name");
		}
		if (options.ipPorts.empty()) {
			throw std::runtime_error("--pr-add-game requires --pr-ipport");
		}
		for (const std::string &ipPort : options.ipPorts) {
			if (!PRGameChannels::isValidIPPort(ipPort)) {
				throw std::runtime_error("Invalid game server address \"" + ipPort + "\" (expected <ip>:<port>)");
			}
		}
		std::vector< std::string > ipPorts = options.ipPorts;
		std::sort(ipPorts.begin(), ipPorts.end());
		if (std::adjacent_find(ipPorts.begin(), ipPorts.end()) != ipPorts.end()) {
			throw std::runtime_error("The same game server address was given more than once");
		}
		if (options.secret && options.secret->empty()) {
			throw std::runtime_error("The secret must not be empty (use --pr-no-verify to disable verification)");
		}

		const bool verify = !options.noVerify;

		mdb::TransactionHolder transaction = db.ensureTransaction();

		std::vector< PRGameChannels::Game > games = loadGames(db, serverID);

		if (const PRGameChannels::Game *existing = PRGameChannels::findGameByCode(games, code)) {
			std::vector< std::string > existingIPPorts = existing->ipPorts;
			std::sort(existingIPPorts.begin(), existingIPPorts.end());

			const bool sameSecret = !verify || !options.secret || *options.secret == existing->secret;
			if (existing->name == *options.name && existingIPPorts == ipPorts && existing->verify == verify
				&& existing->linkTeams == options.linkTeams && sameSecret) {
				qInfo("PR game \"%s\" is already registered on server %u, nothing to do", code.c_str(), serverID);
				return 0;
			}

			throw std::runtime_error("PR game \"" + code
									 + "\" is already registered with different settings; remove it with "
									   "--pr-remove-game first to change it");
		}

		for (const std::string &ipPort : ipPorts) {
			if (const PRGameChannels::Game *other = PRGameChannels::findGameByIPPort(games, ipPort)) {
				throw std::runtime_error("Game server address " + ipPort + " is already registered for PR game \""
										 + other->code + "\"");
			}
		}

		const unsigned int baseChannel = ensureSetup(db, serverID);

		PRGameChannels::Game game = createGameTree(db, serverID, baseChannel, options);
		game.code                 = code;
		game.name                 = *options.name;
		game.ipPorts              = options.ipPorts;
		game.verify               = verify;
		game.linkTeams            = options.linkTeams;

		const bool generateSecret = verify && !options.secret;
		if (verify) {
			game.secret = generateSecret ? PasswordGenerator::generatePassword(GENERATED_SECRET_LENGTH).toStdString()
										 : *options.secret;
		}

		games.push_back(game);
		db.getConfigTable().setConfig(serverID, PRGameChannels::GAMES_CONFIG_KEY, PRGameChannels::gamesToJSON(games));

		transaction.commit();

		qInfo("Registered PR game \"%s\" on server %u (channel %u)", code.c_str(), serverID, game.gameChannel);
		if (generateSecret) {
			qInfo("Generated identity secret for PR game \"%s\": %s", code.c_str(), game.secret.c_str());
			qInfo("Put this value into the game server's realityconfig_admin.py");
		}
		if (!verify) {
			qWarning("Identity verification is disabled for PR game \"%s\": anyone can claim any team and squad",
					 code.c_str());
		}
		qInfo("Restart the server for the change to take effect");

		return 0;
	}

	int listGames(msdb::ServerDatabase &db, unsigned int serverID) {
		for (const PRGameChannels::Game &game : loadGames(db, serverID)) {
			std::cout << game.code << "\t" << game.name << "\t" << joined(game.ipPorts) << "\t"
					  << (game.verify ? "secret=" + game.secret : std::string("no-verify"))
					  << "\tchannel=" << game.gameChannel << std::endl;
		}
		return 0;
	}

	int removeGame(msdb::ServerDatabase &db, unsigned int serverID, const std::string &code) {
		mdb::TransactionHolder transaction = db.ensureTransaction();

		std::vector< PRGameChannels::Game > games = loadGames(db, serverID);

		auto it = std::find_if(games.begin(), games.end(),
							   [&code](const PRGameChannels::Game &game) { return game.code == code; });
		if (it == games.end()) {
			throw std::runtime_error("PR game \"" + code + "\" is not registered on server "
									 + std::to_string(serverID));
		}

		// Removing the game channel removes its sub-channels, ACLs, groups and links along with it
		if (db.getChannelTable().channelExists(serverID, it->gameChannel)) {
			db.getChannelTable().removeChannel(serverID, it->gameChannel);
		}

		games.erase(it);
		db.getConfigTable().setConfig(serverID, PRGameChannels::GAMES_CONFIG_KEY, PRGameChannels::gamesToJSON(games));

		transaction.commit();

		qInfo("Removed PR game \"%s\" and its channels from server %u", code.c_str(), serverID);
		qInfo("Restart the server for the change to take effect");

		return 0;
	}

} // namespace

void addCLIOptions(CLI::App &app, CLIOptions &options) {
	CLI::Option *add = app.add_option("--pr-add-game", options.addGame,
									  "Register a PR:BF2 game server and create its channels. Needs --pr-name and "
									  "--pr-ipport. Does nothing if the game is already registered the same way.")
						   ->option_text("<code>")
						   ->group(CLI_SECTION);
	app.add_option("--pr-name", options.name, "Name of the game's channel (with --pr-add-game).")
		->option_text("<name>")
		->group(CLI_SECTION);
	app.add_option("--pr-ipport", options.ipPorts,
				   "Address(es) the game server reports to players (with --pr-add-game).")
		->option_text("<ip:port>[,<ip:port>...]")
		->delimiter(',')
		->group(CLI_SECTION);
	CLI::Option *secret =
		app.add_option("--pr-secret", options.secret,
					   "Identity secret shared with the game server's realityconfig_admin.py (with --pr-add-game). "
					   "Generated if omitted.")
			->option_text("<secret>")
			->group(CLI_SECTION);
	CLI::Option *noVerify =
		app.add_flag("--pr-no-verify", options.noVerify,
					 "Don't verify player identities for this game (with --pr-add-game). Anyone can then claim "
					 "any team and squad.")
			->group(CLI_SECTION);
	app.add_flag("--pr-link-teams", options.linkTeams,
				 "Link the game's channel with both team channels (with --pr-add-game).")
		->group(CLI_SECTION);
	app.add_option("--pr-server", options.server,
				   "Virtual server to act on. Defaults to the only virtual server in the database.")
		->option_text("<srv>")
		->group(CLI_SECTION);
	CLI::Option *list = app.add_flag("--pr-list-games", options.listGames, "List the registered PR:BF2 game servers.")
							->group(CLI_SECTION);
	CLI::Option *remove = app.add_option("--pr-remove-game", options.removeGame,
										 "Unregister a PR:BF2 game server and delete its channels.")
							  ->option_text("<code>")
							  ->group(CLI_SECTION);

	secret->excludes(noVerify);
	add->excludes(list)->excludes(remove);
	list->excludes(remove);
}

bool isRequested(const CLIOptions &options) {
	return options.addGame || options.listGames || options.removeGame;
}

int run(const CLIOptions &options, const mdb::ConnectionParameter &connectionParameter) {
	try {
		const unsigned int serverID = resolveServer(options, connectionParameter, options.addGame.has_value());

		msdb::ServerDatabase db(connectionParameter.applicability());
		db.init(connectionParameter);

		if (options.listGames) {
			return listGames(db, serverID);
		}
		if (options.removeGame) {
			return removeGame(db, serverID, *options.removeGame);
		}
		return addGame(db, serverID, options);
	} catch (const std::exception &e) {
		qCritical("%s", e.what());
		return 1;
	}
}

} // namespace PRGameAdmin
