// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "SSL.h"

#include "ACL.h"
#include "PRGameChannels.h"

#include <set>
#include <stdexcept>
#include <string>

using namespace PRGameChannels;

namespace {
const std::string PREFIX = std::string("Project Reality: BF2") + '\0';

std::string identityJSON(const std::string &team, const std::string &squad, const std::string &squadLeader,
						 const std::string &commander) {
	return R"({"name":"Player","team":)" + team + R"(,"squad":)" + squad + R"(,"squad_leader":)" + squadLeader
		   + R"(,"commander":)" + commander + R"(,"hash":"abc123","pass":"5209251"})";
}

Game exampleGame() {
	Game game;
	game.code        = "awesome";
	game.name        = "Awesome PR Server";
	game.ipPorts     = { "1.2.3.4:16567", "5.6.7.8:16567" };
	game.secret      = "secret";
	game.gameChannel = 3;
	for (std::size_t team = 0; team < TEAM_COUNT; ++team) {
		const unsigned int base    = 10 + static_cast< unsigned int >(team) * 20;
		game.teams[team].team      = base;
		game.teams[team].commander = base + 1;
		for (unsigned int squad = 0; squad < SQUAD_COUNT; ++squad) {
			game.teams[team].squads[squad] = base + 2 + squad;
		}
	}
	return game;
}
} // namespace

class TestPRGameChannels : public QObject {
	Q_OBJECT
private slots:
	void initTestCase();
	void cleanupTestCase();

	void linked();
	void contextIPPort();
	void validIdentity();
	void invalidIdentity_data();
	void invalidIdentity();
	void identityCredentials();
	void passVectors();
	void passWindow();
	void assignment();
	void findGames();
	void codeAndIPPortValidation();
	void gamesJSONRoundTrip();
	void gamesJSONInvalid();
	void gameTree();
	void gameTreeLinkTeams();
	void aclGroupsAreDefined();
	void setup();
};

void TestPRGameChannels::initTestCase() {
	MumbleSSL::initialize();
}

void TestPRGameChannels::cleanupTestCase() {
	MumbleSSL::destroy();
}

void TestPRGameChannels::linked() {
	QVERIFY(isLinked(PREFIX + R"({"ipport":"1.2.3.4:16567"})"));
	QVERIFY(isLinked("Project Reality: BF2"));
	QVERIFY(!isLinked(""));
	QVERIFY(!isLinked("Project Reality: BF"));
	QVERIFY(!isLinked(std::string("Project Reality: BF2X") + '\0' + "{}"));
	QVERIFY(!isLinked(std::string("Battlefield 2") + '\0' + "{}"));
}

void TestPRGameChannels::contextIPPort() {
	QCOMPARE(parseContextIPPort(PREFIX + R"({"ipport":"1.2.3.4:16567"})").value_or(""), std::string("1.2.3.4:16567"));

	QVERIFY(!parseContextIPPort("Project Reality: BF2"));
	QVERIFY(!parseContextIPPort(PREFIX));
	QVERIFY(!parseContextIPPort(PREFIX + "not json"));
	QVERIFY(!parseContextIPPort(PREFIX + R"(["ipport"])"));
	QVERIFY(!parseContextIPPort(PREFIX + R"({"ipport":16567})"));
	QVERIFY(!parseContextIPPort(PREFIX + R"({"ip":"1.2.3.4:16567"})"));
	QVERIFY(!parseContextIPPort(std::string("Other game") + '\0' + R"({"ipport":"1.2.3.4:16567"})"));
}

void TestPRGameChannels::validIdentity() {
	std::optional< Identity > identity = parseIdentity(identityJSON(R"("blufor")", "3", "true", "false"));
	QVERIFY(identity);
	QVERIFY(identity->team == Team::Blufor);
	QCOMPARE(identity->squad, 3u);
	QVERIFY(identity->squadLeader);
	QVERIFY(!identity->commander);
	QCOMPARE(identity->hash, std::string("abc123"));
	QCOMPARE(identity->pass, std::string("5209251"));

	identity = parseIdentity(identityJSON(R"("opfor")", "0", "false", "true"));
	QVERIFY(identity);
	QVERIFY(identity->team == Team::Opfor);
	QCOMPARE(identity->squad, 0u);
	QVERIFY(identity->commander);

	QVERIFY(parseIdentity(identityJSON(R"("opfor")", "9", "false", "false")));
}

void TestPRGameChannels::invalidIdentity_data() {
	QTest::addColumn< QString >("identity");

	QTest::newRow("empty") << QString();
	QTest::newRow("garbage") << QString("{team:");
	QTest::newRow("list") << QString(R"([{"team":"opfor"}])");
	QTest::newRow("unknown team") << QString::fromStdString(identityJSON(R"("usmc")", "1", "false", "false"));
	QTest::newRow("team not a string") << QString::fromStdString(identityJSON("1", "1", "false", "false"));
	QTest::newRow("squad negative") << QString::fromStdString(identityJSON(R"("opfor")", "-1", "false", "false"));
	QTest::newRow("squad too big") << QString::fromStdString(identityJSON(R"("opfor")", "10", "false", "false"));
	QTest::newRow("squad float") << QString::fromStdString(identityJSON(R"("opfor")", "3.0", "false", "false"));
	QTest::newRow("squad bool") << QString::fromStdString(identityJSON(R"("opfor")", "true", "false", "false"));
	QTest::newRow("squad string") << QString::fromStdString(identityJSON(R"("opfor")", R"("3")", "false", "false"));
	QTest::newRow("leader not bool") << QString::fromStdString(identityJSON(R"("opfor")", "3", "1", "false"));
	QTest::newRow("commander not bool") << QString::fromStdString(identityJSON(R"("opfor")", "3", "false", "null"));
	QTest::newRow("missing commander") << QString(R"({"team":"opfor","squad":1,"squad_leader":false})");
}

void TestPRGameChannels::invalidIdentity() {
	QFETCH(QString, identity);
	QVERIFY(!parseIdentity(identity.toStdString()));
}

void TestPRGameChannels::identityCredentials() {
	const std::string base = R"({"team":"opfor","squad":1,"squad_leader":false,"commander":false)";

	std::optional< Identity > identity = parseIdentity(base + R"(,"hash":12345,"pass":5209251})");
	QVERIFY(identity);
	QCOMPARE(identity->hash, std::string("12345"));
	QCOMPARE(identity->pass, std::string("5209251"));

	identity = parseIdentity(base + "}");
	QVERIFY(identity);
	QVERIFY(identity->hash.empty());
	QVERIFY(identity->pass.empty());

	identity = parseIdentity(base + R"(,"hash":["x"],"pass":1.5})");
	QVERIFY(identity);
	QVERIFY(identity->hash.empty());
	QVERIFY(identity->pass.empty());
}

void TestPRGameChannels::passVectors() {
	// Generated with the algorithm of the mumo prbf2 module (Python's hashlib.sha1 and struct.unpack("<i"))
	QCOMPARE(computePass(29000000, "abc123", "secret"), std::string("5209251"));
	QCOMPARE(computePass(29000001, "abc123", "secret"), std::string("5301038"));
	QCOMPARE(computePass(27000000, "0123456789abcdef0123456789abcdef", "Xy7kP2qR9sT4uV6wZ8aB"), std::string("6597501"));
	QCOMPARE(computePass(1, "", ""), std::string("1665589"));
	QCOMPARE(computePass(29340000, "deadbeefcafebabe", "s3cr3t"), std::string("7743032"));
}

void TestPRGameChannels::passWindow() {
	QVERIFY(verifyPass("abc123", "5209251", "secret", 29000000));
	QVERIFY(verifyPass("abc123", "5209251", "secret", 29000001));
	QVERIFY(verifyPass("abc123", "5209251", "secret", 28999999));
	QVERIFY(!verifyPass("abc123", "5209251", "secret", 29000002));
	QVERIFY(!verifyPass("abc123", "5209251", "secret", 28999998));

	QVERIFY(!verifyPass("abc123", "5209251", "other", 29000000));
	QVERIFY(!verifyPass("abc124", "5209251", "secret", 29000000));
	QVERIFY(!verifyPass("abc123", "", "secret", 29000000));
}

void TestPRGameChannels::assignment() {
	const Game game = exampleGame();

	Identity member{ Team::Blufor, 3, false, false, "", "" };
	Assignment result = assignmentFor(game, member);
	QCOMPARE(result.gameGroups, std::vector< std::string >({ "bf2_awesome_game" }));
	QCOMPARE(result.teamChannel, game.teams[1].team);
	QCOMPARE(result.teamGroups, std::vector< std::string >({ "bf2_team", "bf2_third_squad" }));
	QCOMPARE(result.targetChannel, game.teams[1].squads[3]);

	Identity leader{ Team::Opfor, 0, true, false, "", "" };
	result = assignmentFor(game, leader);
	QCOMPARE(result.teamChannel, game.teams[0].team);
	QCOMPARE(result.teamGroups,
			 std::vector< std::string >({ "bf2_team", "bf2_no_squad", "bf2_no_squad_leader", "bf2_squad_leader" }));
	QCOMPARE(result.targetChannel, game.teams[0].squads[0]);

	Identity commander{ Team::Opfor, 9, false, true, "", "" };
	result = assignmentFor(game, commander);
	QCOMPARE(result.teamGroups,
			 std::vector< std::string >({ "bf2_team", "bf2_ninth_squad", "bf2_commander", "bf2_squad_leader" }));
	QCOMPARE(result.targetChannel, game.teams[0].commander);

	Identity leadingCommander{ Team::Blufor, 1, true, true, "", "" };
	result = assignmentFor(game, leadingCommander);
	QCOMPARE(result.teamGroups, std::vector< std::string >({ "bf2_team", "bf2_first_squad", "bf2_first_squad_leader",
															 "bf2_squad_leader", "bf2_commander" }));
	QCOMPARE(result.targetChannel, game.teams[1].commander);
}

void TestPRGameChannels::findGames() {
	Game other    = exampleGame();
	other.code    = "other";
	other.ipPorts = { "9.9.9.9:16567" };

	const std::vector< Game > games = { exampleGame(), other };

	QCOMPARE(findGameByIPPort(games, "5.6.7.8:16567"), &games[0]);
	QCOMPARE(findGameByIPPort(games, "9.9.9.9:16567"), &games[1]);
	QVERIFY(!findGameByIPPort(games, "1.2.3.4:16568"));
	QVERIFY(!findGameByIPPort(games, ""));

	QCOMPARE(findGameByCode(games, "other"), &games[1]);
	QVERIFY(!findGameByCode(games, "Other"));
}

void TestPRGameChannels::codeAndIPPortValidation() {
	QVERIFY(isValidCode("awesome"));
	QVERIFY(isValidCode("main_0-EU"));
	QVERIFY(!isValidCode(""));
	QVERIFY(!isValidCode("has space"));
	QVERIFY(!isValidCode("dots.not.allowed"));
	QVERIFY(!isValidCode(std::string(33, 'a')));

	QVERIFY(isValidIPPort("1.2.3.4:16567"));
	QVERIFY(isValidIPPort("pr.example.com:16567"));
	QVERIFY(!isValidIPPort("1.2.3.4"));
	QVERIFY(!isValidIPPort(":16567"));
	QVERIFY(!isValidIPPort("1.2.3.4:"));
	QVERIFY(!isValidIPPort("1.2.3.4:0"));
	QVERIFY(!isValidIPPort("1.2.3.4:65536"));
	QVERIFY(!isValidIPPort("1.2.3.4:16a67"));
	QVERIFY(!isValidIPPort("1.2.3.4,5.6.7.8:16567"));
	QVERIFY(!isValidIPPort("1.2.3.4 :16567"));
}

void TestPRGameChannels::gamesJSONRoundTrip() {
	Game unverified      = exampleGame();
	unverified.code      = "open";
	unverified.secret    = "";
	unverified.verify    = false;
	unverified.linkTeams = true;

	const std::vector< Game > games  = { exampleGame(), unverified };
	const std::vector< Game > parsed = gamesFromJSON(gamesToJSON(games));

	QCOMPARE(parsed.size(), games.size());
	for (std::size_t i = 0; i < games.size(); ++i) {
		QCOMPARE(parsed[i].code, games[i].code);
		QCOMPARE(parsed[i].name, games[i].name);
		QCOMPARE(parsed[i].ipPorts, games[i].ipPorts);
		QCOMPARE(parsed[i].secret, games[i].secret);
		QCOMPARE(parsed[i].verify, games[i].verify);
		QCOMPARE(parsed[i].linkTeams, games[i].linkTeams);
		QCOMPARE(parsed[i].gameChannel, games[i].gameChannel);
		for (std::size_t team = 0; team < TEAM_COUNT; ++team) {
			QCOMPARE(parsed[i].teams[team].team, games[i].teams[team].team);
			QCOMPARE(parsed[i].teams[team].commander, games[i].teams[team].commander);
			QVERIFY(parsed[i].teams[team].squads == games[i].teams[team].squads);
		}
	}

	QVERIFY(gamesFromJSON("").empty());
	QVERIFY(gamesFromJSON("[]").empty());
}

void TestPRGameChannels::gamesJSONInvalid() {
	QVERIFY_THROWS_EXCEPTION(std::invalid_argument, gamesFromJSON("{"));
	QVERIFY_THROWS_EXCEPTION(std::invalid_argument, gamesFromJSON("{}"));
	QVERIFY_THROWS_EXCEPTION(std::invalid_argument, gamesFromJSON("[1]"));
	QVERIFY_THROWS_EXCEPTION(std::invalid_argument, gamesFromJSON(R"([{"code":"a"}])"));

	std::string json = gamesToJSON({ exampleGame() });
	json.replace(json.find("\"game\":3"), 8, "\"game\":-3");
	QVERIFY_THROWS_EXCEPTION(std::invalid_argument, gamesFromJSON(json));
}

void TestPRGameChannels::gameTree() {
	const std::vector< ChannelSpec > tree = gameTreeSpec("awesome", "Awesome PR Server", false);

	QCOMPARE(tree.size(), std::size_t(1 + TEAM_COUNT * (2 + SQUAD_COUNT)));

	QVERIFY(tree[0].role == ChannelSpec::Role::Game);
	QCOMPARE(tree[0].name, std::string("Awesome PR Server"));
	QCOMPARE(tree[0].parent, -1);
	QCOMPARE(tree[0].groups, std::vector< std::string >({ "bf2_awesome_game", "bf2_awesome_admin" }));

	std::set< std::string > teamNames;
	for (std::size_t i = 1; i < tree.size(); ++i) {
		const ChannelSpec &channel = tree[i];
		QVERIFY(channel.team.has_value());
		QVERIFY(channel.parent >= 0 && static_cast< std::size_t >(channel.parent) < i);

		switch (channel.role) {
			case ChannelSpec::Role::Team:
				QCOMPARE(channel.parent, 0);
				QVERIFY(!channel.linkedToParent);
				QCOMPARE(channel.name, std::string(*channel.team == Team::Opfor ? "Team 1" : "Team 2"));
				teamNames.insert(channel.name);
				break;
			case ChannelSpec::Role::Commander:
				QCOMPARE(channel.name, std::string("Commander"));
				QCOMPARE(channel.position, -1);
				QVERIFY(channel.linkedToParent);
				QVERIFY(tree[static_cast< std::size_t >(channel.parent)].role == ChannelSpec::Role::Team);
				QVERIFY(tree[static_cast< std::size_t >(channel.parent)].team == channel.team);
				break;
			case ChannelSpec::Role::Squad:
				QCOMPARE(channel.name,
						 std::string(channel.squad == 0 ? "No Squad" : "Squad " + std::to_string(channel.squad)));
				QVERIFY(channel.linkedToParent);
				QVERIFY(tree[static_cast< std::size_t >(channel.parent)].team == channel.team);
				break;
			default:
				QFAIL("unexpected channel role");
		}
	}
	QCOMPARE(teamNames.size(), TEAM_COUNT);

	// Outsiders must not be able to enter or talk in a team's tree
	const ChannelSpec &team = tree[1];
	QCOMPARE(team.acls[0].group, std::string("all"));
	QVERIFY(team.acls[0].applySubs);
	QVERIFY(team.acls[0].deny & ChanACL::Enter);
	QVERIFY(team.acls[0].deny & ChanACL::Speak);
	QVERIFY(team.acls[0].deny & ChanACL::Whisper);
}

void TestPRGameChannels::gameTreeLinkTeams() {
	for (const ChannelSpec &channel : gameTreeSpec("awesome", "Awesome", true)) {
		if (channel.role == ChannelSpec::Role::Team) {
			QVERIFY(channel.linkedToParent);
		}
	}
}

void TestPRGameChannels::aclGroupsAreDefined() {
	const SetupSpec setup                 = setupSpec();
	const std::vector< ChannelSpec > tree = gameTreeSpec("awesome", "Awesome", false);

	std::set< std::string > defined(setup.rootGroups.begin(), setup.rootGroups.end());
	defined.insert("admin");
	for (const ChannelSpec &channel : tree) {
		defined.insert(channel.groups.begin(), channel.groups.end());
	}

	std::vector< ACLSpec > acls = setup.rootACLs;
	for (const ChannelSpec &channel : setup.channels) {
		acls.insert(acls.end(), channel.acls.begin(), channel.acls.end());
	}
	for (const ChannelSpec &channel : tree) {
		acls.insert(acls.end(), channel.acls.begin(), channel.acls.end());
	}

	for (const ACLSpec &acl : acls) {
		const std::string name = acl.group[0] == '~' ? acl.group.substr(1) : acl.group;
		if (name == "all") {
			continue;
		}
		QVERIFY2(defined.count(name), name.c_str());
	}

	// Every group a sorted player is put into must be one the ACLs can refer to
	const Game game = exampleGame();
	for (bool leader : { false, true }) {
		for (bool commander : { false, true }) {
			for (unsigned int squad = 0; squad < SQUAD_COUNT; ++squad) {
				const Assignment assignment = assignmentFor(game, { Team::Opfor, squad, leader, commander, "", "" });
				for (const std::string &group : assignment.gameGroups) {
					QVERIFY2(defined.count(group), group.c_str());
				}
				for (const std::string &group : assignment.teamGroups) {
					if (group.find("_squad_leader") != std::string::npos && group != "bf2_squad_leader") {
						// Not referenced by any ACL, kept for parity with mumo
						continue;
					}
					QVERIFY2(defined.count(group), group.c_str());
				}
			}
		}
	}
}

void TestPRGameChannels::setup() {
	const SetupSpec spec = setupSpec();

	QCOMPARE(spec.channels.size(), std::size_t(2));
	QVERIFY(spec.channels[0].role == ChannelSpec::Role::Base);
	QCOMPARE(spec.channels[0].name, std::string("PR BF2 Game Servers"));
	QCOMPARE(spec.channels[0].position, 1);
	QVERIFY(spec.channels[1].role == ChannelSpec::Role::Lobby);
	QCOMPARE(spec.channels[1].name, std::string("Lobby"));
	QCOMPARE(spec.channels[1].position, 2);

	bool deniesSelfRegister = false;
	for (const ACLSpec &acl : spec.rootACLs) {
		QVERIFY(!(acl.allow & ChanACL::MakeTempChannel));
		deniesSelfRegister |= acl.group == "all" && (acl.deny & ChanACL::SelfRegister);
	}
	QVERIFY(deniesSelfRegister);
}

QTEST_MAIN(TestPRGameChannels)
#include "TestPRGameChannels.moc"
