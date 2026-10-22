// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MURMUR_PRGAMESORTING_H_
#define MUMBLE_MURMUR_PRGAMESORTING_H_

class Server;
class ServerUser;

namespace PRGameSorting {

/// Puts the user into the temporary groups of their Project Reality: BF2 game, team and squad and moves them to
/// their squad channel, based on their plugin context and identity. Users that are not (or no longer) validly in a
/// registered game lose those groups and are moved out of the game's team channels.
///
/// Must be called on the main thread while not holding Server::qrwlVoiceThread.
void sortUser(Server &server, ServerUser &user);

} // namespace PRGameSorting

#endif // MUMBLE_MURMUR_PRGAMESORTING_H_
