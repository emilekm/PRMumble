// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PRVoiceRouting.h"

namespace PRVoiceRouting {

float Route::gainFor(float speakerX) const {
	switch (ear) {
		case Ear::Left:
			return speakerX < 0.0f ? 1.0f : 0.0f;
		case Ear::Right:
			return speakerX > 0.0f ? 1.0f : 0.0f;
		case Ear::Both:
			break;
	}
	return 1.0f;
}

std::optional< Route > route(bool isShout, bool speakerInListenerChannel, const Config &config) {
	if (!isShout || !config.canPan) {
		return std::nullopt;
	}

	const Ear ear = speakerInListenerChannel ? config.squadRadioEar : config.squadLeaderEar;
	if (ear == Ear::Both) {
		return std::nullopt;
	}

	return Route{ ear };
}

} // namespace PRVoiceRouting
