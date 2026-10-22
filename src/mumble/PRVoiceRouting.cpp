// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PRVoiceRouting.h"

#include <atomic>

namespace PRVoiceRouting {

namespace {
	std::atomic< bool > squadRadioMuted  = false;
	std::atomic< bool > squadLeaderMuted = false;

	std::atomic< bool > &mutedFlag(Category category) {
		return category == Category::SquadRadio ? squadRadioMuted : squadLeaderMuted;
	}
} // namespace

float Route::gainFor(float speakerX) const {
	if (muted) {
		return 0.0f;
	}

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
	if (!isShout) {
		return std::nullopt;
	}

	const Ear ear      = speakerInListenerChannel ? config.squadRadioEar : config.squadLeaderEar;
	const bool muted   = speakerInListenerChannel ? config.squadRadioMuted : config.squadLeaderMuted;
	const Route result = { config.canPan ? ear : Ear::Both, muted };
	if (result.ear == Ear::Both && !result.muted) {
		return std::nullopt;
	}

	return result;
}

bool isMuted(Category category) {
	return mutedFlag(category);
}

bool toggleMuted(Category category) {
	std::atomic< bool > &flag = mutedFlag(category);
	const bool muted          = !flag;
	flag                      = muted;
	return muted;
}

void unmuteAll() {
	squadRadioMuted  = false;
	squadLeaderMuted = false;
}

} // namespace PRVoiceRouting
