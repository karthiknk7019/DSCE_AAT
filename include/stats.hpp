// -----------------------------------------------------------------------------
// stats.hpp - OPTIONAL instrumentation for demos and the web UI.
//
// This header is additive: the graded interface in journey.hpp is unchanged.
// `find_journey` keeps its exact signature and behaviour; the wrapper below
// runs the same search and additionally reports what the BFS actually did.
//
// Used by src/server.cpp to answer "why is this the minimum?" without
// duplicating the algorithm anywhere.
// -----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

#include "journey.hpp"

namespace journey {

// One record per stop first discovered by the search. The index in these
// vectors is discovery order, which is BFS order.
struct QueryStats {
  std::vector<std::string> reached_stops;
  std::vector<int> reached_buses;  // buses needed to reach that stop

  // Routes in the order the BFS expanded them. `expanded_route_buses[i]` is
  // the layer the route was expanded from (= the buses needed to reach it).
  std::vector<std::string> expanded_routes;
  std::vector<int> expanded_route_buses;

  int inactive_routes_skipped = 0;

  // Largest bus count among reached stops (0 for a 0-bus journey).
  int max_buses() const {
    int best = 0;
    for (std::size_t i = 0; i < reached_buses.size(); ++i) {
      if (reached_buses[i] > best) {
        best = reached_buses[i];
      }
    }
    return best;
  }
};

// Same search as find_journey, but always fills `stats` (even when no journey
// exists, so a caller can show the reachable island). `stats` must not be null.
bool find_journey_stats(const Network& network, const std::string& source,
                        const std::string& destination,
                        const std::vector<std::string>& inactive_routes,
                        Journey& out, QueryStats& stats);

}  // namespace journey
