// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PRVOICEROUTING_H_
#define MUMBLE_MUMBLE_PRVOICEROUTING_H_

#include <optional>

/// Plays Project Reality squad radio and squad leader voice on the ears the user chose. Both arrive as channel
/// whispers (shouts) and are told apart by where the speaker is: shouts from the listener's own channel are the
/// squad radio, shouts from any other channel come from squad leaders or the commander.
namespace PRVoiceRouting {

enum class Ear { Both, Left, Right };

struct Config {
	Ear squadRadioEar  = Ear::Both;
	Ear squadLeaderEar = Ear::Both;
	/// False for mono output, which has no sides to route to
	bool canPan = false;
};

/// How to play speech that doesn't play the default way
struct Route {
	Ear ear;

	/// The gain for a speaker at the given horizontal position, negative being left
	float gainFor(float speakerX) const;
};

std::optional< Route > route(bool isShout, bool speakerInListenerChannel, const Config &config);

} // namespace PRVoiceRouting

#endif // MUMBLE_MUMBLE_PRVOICEROUTING_H_
