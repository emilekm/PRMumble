// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtCore>
#include <QtTest>

#include "PRVoiceRouting.h"

using namespace PRVoiceRouting;

namespace {
Config splitConfig() {
	Config config;
	config.squadRadioEar  = Ear::Left;
	config.squadLeaderEar = Ear::Right;
	config.canPan         = true;
	return config;
}
} // namespace

class TestPRVoiceRouting : public QObject {
	Q_OBJECT
private slots:
	void defaultConfigRoutesNothing() {
		Config config;
		config.canPan = true;

		QVERIFY(!route(true, true, config));
		QVERIFY(!route(true, false, config));
	}

	void routesShoutsByChannel() {
		const std::optional< Route > squadRadio = route(true, true, splitConfig());
		QVERIFY(squadRadio);
		QCOMPARE(squadRadio->ear, Ear::Left);

		const std::optional< Route > squadLeader = route(true, false, splitConfig());
		QVERIFY(squadLeader);
		QCOMPARE(squadLeader->ear, Ear::Right);
	}

	void ignoresSpeechOtherThanShouts() {
		QVERIFY(!route(false, true, splitConfig()));
		QVERIFY(!route(false, false, splitConfig()));
	}

	void ignoresEarsWithoutSidesToPanTo() {
		Config config = splitConfig();
		config.canPan = false;

		QVERIFY(!route(true, true, config));
		QVERIFY(!route(true, false, config));
	}

	void routesOnlyTheCategoryWithAnEar() {
		Config config        = splitConfig();
		config.squadRadioEar = Ear::Both;

		QVERIFY(!route(true, true, config));
		QVERIFY(route(true, false, config));
	}

	void pansHard() {
		const Route left{ Ear::Left, false };
		QCOMPARE(left.gainFor(-1.0f), 1.0f);
		QCOMPARE(left.gainFor(-0.25f), 1.0f);
		QCOMPARE(left.gainFor(0.0f), 0.0f);
		QCOMPARE(left.gainFor(1.0f), 0.0f);

		const Route right{ Ear::Right, false };
		QCOMPARE(right.gainFor(-1.0f), 0.0f);
		QCOMPARE(right.gainFor(0.0f), 0.0f);
		QCOMPARE(right.gainFor(0.25f), 1.0f);
		QCOMPARE(right.gainFor(1.0f), 1.0f);
	}

	void routesMutedShoutsWithoutAnEar() {
		Config config           = {};
		config.squadLeaderMuted = true;

		QVERIFY(!route(true, true, config));

		const std::optional< Route > squadLeader = route(true, false, config);
		QVERIFY(squadLeader);
		QVERIFY(squadLeader->muted);
		QCOMPARE(squadLeader->gainFor(-1.0f), 0.0f);
		QCOMPARE(squadLeader->gainFor(0.0f), 0.0f);
		QCOMPARE(squadLeader->gainFor(1.0f), 0.0f);
	}

	void doesNotMuteSpeechOtherThanShouts() {
		Config config           = {};
		config.squadRadioMuted  = true;
		config.squadLeaderMuted = true;

		QVERIFY(!route(false, true, config));
		QVERIFY(!route(false, false, config));
	}

	void togglesMutes() {
		unmuteAll();
		QVERIFY(toggleMuted(Category::SquadRadio));
		QVERIFY(isMuted(Category::SquadRadio));
		QVERIFY(!isMuted(Category::SquadLeader));

		QVERIFY(toggleMuted(Category::SquadLeader));
		QVERIFY(!toggleMuted(Category::SquadRadio));
		QVERIFY(!isMuted(Category::SquadRadio));
		QVERIFY(isMuted(Category::SquadLeader));

		unmuteAll();
		QVERIFY(!isMuted(Category::SquadRadio));
		QVERIFY(!isMuted(Category::SquadLeader));
	}
};

QTEST_MAIN(TestPRVoiceRouting)
#include "TestPRVoiceRouting.moc"
