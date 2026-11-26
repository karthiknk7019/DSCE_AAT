// -----------------------------------------------------------------------------
// journey.hpp - FROZEN CONTRACT. Read docs/CONTRACT.md before changing anything.
//
//   Role 1 implements build_network / find_journey in src/graph.cpp
//   Role 2 implements run_cli in src/cli.cpp
//   Role 3 tests against this header in tests/
//
// Changing a signature here needs a PR reviewed by all three members.
// -----------------------------------------------------------------------------
#pragma once

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace journey {

// One bus route: a name plus the stops it serves, in the order given.
// NOTE: the order is informational only. A route can be ridden in either
// direction; the planner ignores timetables, distances and travel time.
struct Route {
  std::string name;
  std::vector<std::string> stops;
};

// Everything that can be computed ONCE for a network and reused by every
// query. Build it with build_network(), never inside the query loop.
struct Network {
  std::vector<Route> routes;

  // stop -> indices into `routes` that serve that stop.
  // Role 1 owns the contents of this index and may extend it.
  std::unordered_map<std::string, std::vector<int>> stop_to_routes;
};

// One bus ride: board `route` at `from`, get off at `to`.
struct Leg {
  std::string route;
  std::string from;
  std::string to;
};

// A complete journey. legs.size() == number of buses boarded.
// An empty `legs` means the passenger is already at the destination (0 buses).
struct Journey {
  std::vector<Leg> legs;
};

// Builds the query-independent index. Called once per network.
Network build_network(const std::vector<Route>& routes);

// Finds a journey using the MINIMUM number of buses.
//   - returns true and fills `out` on success
//   - returns false when no journey exists
//   - source == destination is a successful 0-bus journey
//   - routes named in `inactive_routes` must not be boarded
// Must not rebuild the index: everything needed is already in `network`.
bool find_journey(const Network& network, const std::string& source,
                  const std::string& destination,
                  const std::vector<std::string>& inactive_routes,
                  Journey& out);

// Reads the network, builds the index once, then answers query lines.
// Returns 0 if the network was read successfully, 1 on malformed network
// input. A query with no journey is not an error (still returns 0).
// Input/output grammar: docs/CONTRACT.md.
int run_cli(std::istream& in, std::ostream& out);

}  // namespace journey
