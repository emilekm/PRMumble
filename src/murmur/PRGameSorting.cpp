// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PRGameSorting.h"

#include "Channel.h"
#include "Meta.h"
#include "PRGameChannels.h"
#include "Server.h"
#include "ServerUser.h"

#include <QDateTime>
#include <QStringList>

#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace PRGameSorting {

namespace {

	using PRGameChannels::Game;

	/// Unregistered users have no user ID, so temporary groups are always tied to the session
	constexpr int NO_USER_ID = -1;

	const std::vector< Game > &registeredGames(Server &server) {
		// Games only change through command-line actions while the server is stopped, so they are read once
		static std::unordered_map< unsigned int, std::vector< Game > > s_games;

		auto it = s_games.find(server.iServerNum);
		if (it != s_games.end()) {
			return it->second;
		}

		std::string json;
		meta->dbWrapper.getConfigurationTo(server.iServerNum, PRGameChannels::GAMES_CONFIG_KEY, json);

		std::vector< Game > games;
		try {
			games = PRGameChannels::gamesFromJSON(json);
		} catch (const std::invalid_argument &e) {
			server.log(QString("PR: Ignoring the invalid list of games: %1").arg(QString::fromUtf8(e.what())));
		}

		for (const Game &game : games) {
			if (!game.verify) {
				server.log(QString("PR: WARNING: Identity verification is disabled for game \"%1\"")
							   .arg(QString::fromStdString(game.code)));
			}
		}

		return s_games.emplace(server.iServerNum, std::move(games)).first->second;
	}

	QStringList toQStringList(const std::vector< std::string > &values) {
		QStringList list;
		for (const std::string &value : values) {
			list << QString::fromStdString(value);
		}
		return list;
	}

	bool isInTeamTree(const Channel *channel, const Game &game) {
		for (; channel; channel = channel->cParent) {
			for (const PRGameChannels::TeamChannels &team : game.teams) {
				if (channel->iId == team.team) {
					return true;
				}
			}
		}
		return false;
	}

	void moveUser(Server &server, ServerUser &user, Channel &channel) {
		if (user.cChannel != &channel) {
			server.setUserState(&user, &channel, user.bMute, user.bDeaf, user.bSuppress, user.bPrioritySpeaker,
								user.qsName, user.qsComment);
		}
	}

	bool isVerified(Server &server, ServerUser &user, const Game &game, const PRGameChannels::Identity &identity) {
		if (!game.verify) {
			return true;
		}

		const long long currentMinute = QDateTime::currentSecsSinceEpoch() / 60;
		if (PRGameChannels::verifyPass(identity.hash, identity.pass, game.secret, currentMinute)) {
			return true;
		}

		server.log(&user, QString("PR: WARNING: Identity verification failed for game \"%1\" (hash \"%2\", "
								  "certificate %3, address %4)")
							  .arg(QString::fromStdString(game.code), QString::fromStdString(identity.hash),
								   user.qsHash, server.addressToString(user.peerAddress(), user.peerPort())));
		return false;
	}

} // namespace

void sortUser(Server &server, ServerUser &user) {
	const std::vector< Game > &games = registeredGames(server);
	if (games.empty()) {
		return;
	}

	const int session = static_cast< int >(user.uiSession);
	const bool linked = PRGameChannels::isLinked(user.ssContext);

	server.setTempGroups(NO_USER_ID, session, nullptr,
						 linked ? QStringList(QString::fromLatin1(PRGameChannels::LINKED_GROUP)) : QStringList());

	for (const Game &game : games) {
		if (Channel *gameChannel = server.qhChannels.value(game.gameChannel)) {
			server.clearTempGroups(&user, gameChannel);
		}
	}

	const Game *game = nullptr;
	std::optional< std::string > ipPort;
	std::optional< PRGameChannels::Identity > identity;
	if (!user.qsIdentity.isEmpty()) {
		identity = PRGameChannels::parseIdentity(user.qsIdentity.toStdString());
	}
	if (linked) {
		ipPort = PRGameChannels::parseContextIPPort(user.ssContext);
		if (ipPort) {
			game = PRGameChannels::findGameByIPPort(games, *ipPort);
		}
	}

	const std::string contextPrefix = user.ssContext.substr(0, user.ssContext.find('\0'));
	qDebug("%u => <%u:%s(%d)> PR: context \"%s\", identity %s, ipport \"%s\", game \"%s\"", server.iServerNum,
		   user.uiSession, qPrintable(user.qsName), user.iId, contextPrefix.c_str(),
		   user.qsIdentity.isEmpty() ? "missing" : (identity ? "valid" : "invalid"), ipPort ? ipPort->c_str() : "",
		   game ? game->code.c_str() : "");

	if (game && identity && isVerified(server, user, *game, *identity)) {
		const PRGameChannels::Assignment assignment = PRGameChannels::assignmentFor(*game, *identity);

		Channel *gameChannel   = server.qhChannels.value(game->gameChannel);
		Channel *teamChannel   = server.qhChannels.value(assignment.teamChannel);
		Channel *targetChannel = server.qhChannels.value(assignment.targetChannel);
		if (!gameChannel || !teamChannel || !targetChannel) {
			server.log(QString("PR: Channels of game \"%1\" are missing, re-register the game")
						   .arg(QString::fromStdString(game->code)));
			return;
		}

		server.setTempGroups(NO_USER_ID, session, gameChannel, toQStringList(assignment.gameGroups));
		server.setTempGroups(NO_USER_ID, session, teamChannel, toQStringList(assignment.teamGroups));
		moveUser(server, user, *targetChannel);
		return;
	}

	for (const Game &registered : games) {
		if (isInTeamTree(user.cChannel, registered)) {
			if (Channel *gameChannel = server.qhChannels.value(registered.gameChannel)) {
				moveUser(server, user, *gameChannel);
			}
			break;
		}
	}
}

} // namespace PRGameSorting
