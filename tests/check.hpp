// Tiny dependency-free test helper. Role 3 owns this file.
#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "journey.hpp"

inline int check_failures = 0;

template <typename A, typename B>
void check_eq(const A& actual, const B& expected, const char* expr,
              const char* file, int line) {
  if (!(actual == expected)) {
    std::cerr << file << ":" << line << "  FAILED: " << expr
              << "  (actual: " << actual << ", expected: " << expected
              << ")\n";
    ++check_failures;
  }
}

#define CHECK_TRUE(cond)                                                   \
  do {                                                                     \
    if (!(cond)) {                                                         \
      std::cerr << __FILE__ << ":" << __LINE__ << "  FAILED: " #cond "\n"; \
      ++check_failures;                                                    \
    }                                                                      \
  } while (0)

#define CHECK_EQ(actual, expected) \
  check_eq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)

// -----------------------------------------------------------------------------
// Independent contract check (Role 3 deliverable).
//
// A journey is valid for (source, destination, inactive) when, per
// docs/CONTRACT.md section 4:
//   1. it starts at `source` and ends at `destination`;
//   2. consecutive legs chain: legs[i].to == legs[i+1].from;
//   3. every leg's route is active and serves BOTH of its stops;
//   4. an empty journey is allowed only when source == destination.
//
// This deliberately re-derives validity from the raw route list and the leg
// fields. It never looks at find_journey()'s internal parent links, so a bug in
// the search cannot make its own output look valid.
// -----------------------------------------------------------------------------
inline bool route_is_active(const std::string& name,
                            const std::vector<std::string>& inactive) {
  for (const std::string& closed : inactive) {
    if (closed == name) return false;
  }
  return true;
}

inline const journey::Route* find_route(const journey::Network& net,
                                        const std::string& name) {
  for (const journey::Route& route : net.routes) {
    if (route.name == name) return &route;
  }
  return nullptr;
}

inline bool route_serves(const journey::Route& route, const std::string& stop) {
  for (const std::string& served : route.stops) {
    if (served == stop) return true;
  }
  return false;
}

inline bool journey_is_valid(const journey::Network& net,
                             const std::string& source,
                             const std::string& destination,
                             const std::vector<std::string>& inactive,
                             const journey::Journey& journey) {
  if (journey.legs.empty()) return source == destination;
  if (journey.legs.front().from != source) return false;
  if (journey.legs.back().to != destination) return false;

  for (std::size_t i = 0; i + 1 < journey.legs.size(); ++i) {
    if (journey.legs[i].to != journey.legs[i + 1].from) return false;
  }

  for (const journey::Leg& leg : journey.legs) {
    if (!route_is_active(leg.route, inactive)) return false;
    const journey::Route* route = find_route(net, leg.route);
    if (route == nullptr) return false;
    if (!route_serves(*route, leg.from) || !route_serves(*route, leg.to)) {
      return false;
    }
  }
  return true;
}
