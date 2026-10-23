// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PRVoiceConfig.h"

#include "Settings.h"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

const QString PRVoiceConfig::name = QLatin1String("PRVoiceConfig");

static ConfigWidget *PRVoiceConfigNew(Settings &st) {
	return new PRVoiceConfig(st);
}

static ConfigRegistrar registrarPRVoiceConfig(1020, PRVoiceConfigNew);

namespace {
QComboBox *earComboBox(QWidget *parent) {
	QComboBox *comboBox = new QComboBox(parent);
	// The item indices are the values of PRVoiceRouting::Ear
	comboBox->addItem(PRVoiceConfig::tr("Both ears"));
	comboBox->addItem(PRVoiceConfig::tr("Left ear"));
	comboBox->addItem(PRVoiceConfig::tr("Right ear"));
	return comboBox;
}
} // namespace

PRVoiceConfig::PRVoiceConfig(Settings &st) : ConfigWidget(st) {
	QGroupBox *group  = new QGroupBox(tr("Radio"), this);
	QFormLayout *form = new QFormLayout(group);

	m_squadRadioEar = earComboBox(group);
	m_squadRadioEar->setToolTip(tr("The ear to hear your squad's radio on"));
	form->addRow(tr("Squad radio"), m_squadRadioEar);

	m_squadLeaderEar = earComboBox(group);
	m_squadLeaderEar->setToolTip(tr("The ear to hear other squad leaders and the commander on"));
	form->addRow(tr("SL / Command"), m_squadLeaderEar);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(group);
	layout->addStretch();
}

QString PRVoiceConfig::title() const {
	return tr("Project Reality");
}

const QString &PRVoiceConfig::getName() const {
	return PRVoiceConfig::name;
}

QIcon PRVoiceConfig::icon() const {
	return QIcon(QLatin1String("skin:config_audio_output.png"));
}

void PRVoiceConfig::load(const Settings &r) {
	loadComboBox(m_squadRadioEar, static_cast< int >(r.prSquadRadioEar));
	loadComboBox(m_squadLeaderEar, static_cast< int >(r.prSquadLeaderEar));
}

void PRVoiceConfig::save() const {
	s.prSquadRadioEar  = static_cast< PRVoiceRouting::Ear >(m_squadRadioEar->currentIndex());
	s.prSquadLeaderEar = static_cast< PRVoiceRouting::Ear >(m_squadLeaderEar->currentIndex());
}
