// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MURMUR_PRGAMEADMIN_H_
#define MUMBLE_MURMUR_PRGAMEADMIN_H_

#include <optional>
#include <string>
#include <vector>

namespace CLI {
class App;
}

namespace mumble {
namespace db {
	class ConnectionParameter;
}
} // namespace mumble

/// Command-line actions that register and remove Project Reality: BF2 game servers. Like --set-su-pw, they work on
/// the database directly and are meant to be run while the server is stopped.
namespace PRGameAdmin {

struct CLIOptions {
	std::optional< std::string > addGame;
	std::optional< std::string > name;
	std::vector< std::string > ipPorts;
	std::optional< std::string > secret;
	bool noVerify  = false;
	bool linkTeams = false;
	std::optional< unsigned int > server;
	bool listGames = false;
	std::optional< std::string > removeGame;
};

void addCLIOptions(CLI::App &app, CLIOptions &options);
bool isRequested(const CLIOptions &options);
/// @returns The process exit code
int run(const CLIOptions &options, const ::mumble::db::ConnectionParameter &connectionParameter);

} // namespace PRGameAdmin

#endif // MUMBLE_MURMUR_PRGAMEADMIN_H_
