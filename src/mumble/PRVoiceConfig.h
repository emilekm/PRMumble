// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PRVOICECONFIG_H_
#define MUMBLE_MUMBLE_PRVOICECONFIG_H_

#include "ConfigWidget.h"

class QComboBox;

class PRVoiceConfig : public ConfigWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PRVoiceConfig)

	QComboBox *m_squadRadioEar;
	QComboBox *m_squadLeaderEar;

public:
	/// The unique name of this ConfigWidget
	static const QString name;
	PRVoiceConfig(Settings &st);
	QString title() const Q_DECL_OVERRIDE;
	const QString &getName() const Q_DECL_OVERRIDE;
	QIcon icon() const Q_DECL_OVERRIDE;
public slots:
	void save() const Q_DECL_OVERRIDE;
	void load(const Settings &r) Q_DECL_OVERRIDE;
};

#endif // MUMBLE_MUMBLE_PRVOICECONFIG_H_
