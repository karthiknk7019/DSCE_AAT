// Role 1 owns this file. Do not edit it if you are Role 2 or Role 3.
#include "journey.hpp"

namespace journey {

// The index below is intentionally simple and already works: for every stop
// it records which routes serve it. Keep it, or replace the unordered_map
// with your own hash table (Module 5) if your evaluator asks for one.
Network build_network(const std::vector<Route>& routes) {
  Network network;
  network.routes = routes;
  for (int r = 0; r < static_cast<int>(network.routes.size()); ++r) {
    for (const std::string& stop : network.routes[r].stops) {
      network.stop_to_routes[stop].push_back(r);
    }
  }
  return network;
}

// -----------------------------------------------------------------------------
// TODO(role-1): implement minimum-bus BFS.
//
// Suggested plan (details in docs/ROLES.md -> Role 1):
//   1. source == destination -> success with 0 legs, return true
//   2. BFS over stops. A "layer" is one bus boarding, so the first time the
//      destination is reached you have the minimum number of buses.
//   3. From a stop, use network.stop_to_routes[stop] to find every route that
//      serves it. Skip inactive routes and routes already expanded - this is
//      what stops a heavily overlapping network from being scanned twice.
//   4. For every not-yet-visited stop on that route: remember
//      (parent stop, route), push it, and continue.
//   5. Walk parent links back from the destination to rebuild `out.legs`
//      in travel order.
//
// Course rules: write your own queue for the BFS frontier (circular array or
// linked queue, Module 2). Do not use std::queue and do not use a library
// graph/BFS implementation.
// -----------------------------------------------------------------------------
bool find_journey(const Network& network, const std::string& source,
                  const std::string& destination,
                  const std::vector<std::string>& inactive_routes,
                  Journey& out) {
  (void)network;
  (void)source;
  (void)destination;
  (void)inactive_routes;
  out = Journey{};  // stub: Role 1 replaces this
  return false;
}

}  // namespace journey
