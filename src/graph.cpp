// Role 1 owns this file. Do not edit it if you are Role 2 or Role 3.
#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "journey.hpp"

namespace journey {
namespace {

// Hand-written circular queue of stop names (Module 2: queue ADT).
// The capacity is the number of distinct stops, and a stop is enqueued at
// most once per query, so the ring can never overflow.
class StopQueue {
 public:
  explicit StopQueue(std::size_t capacity)
      : data_(capacity == 0 ? 1 : capacity) {}

  bool empty() const { return size_ == 0; }

  void push(const std::string& stop) {
    data_[tail_] = stop;
    tail_ = (tail_ + 1) % data_.size();
    ++size_;
  }

  std::string pop() {
    std::string stop = data_[head_];
    head_ = (head_ + 1) % data_.size();
    --size_;
    return stop;
  }

 private:
  std::vector<std::string> data_;
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t size_ = 0;
};

}  // namespace

// The index below is intentionally simple: for every stop it records which
// routes serve it. This is the adjacency list of a bipartite stop <-> route
// graph. It is built once; queries never touch it again.
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

// Minimum-bus BFS.
//
// A stop -> route edge costs one bus, and a route -> stop edge costs zero
// (you stay on the bus). BFS over that bipartite graph therefore explores
// stops in order of boardings, so the first layer that reaches the
// destination is the minimum number of buses, regardless of how many stops
// the ride crosses.
//
// Each route is expanded at most once per query (`route_expanded`). The first
// stop that reaches a route is a closest one, so a later expansion could only
// produce a longer journey; skipping it keeps a query at O(A), where A is the
// number of stop appearances. Inactive routes are filtered here rather than in
// the index, so closures never force a rebuild.
bool find_journey(const Network& network, const std::string& source,
                  const std::string& destination,
                  const std::vector<std::string>& inactive_routes,
                  Journey& out) {
  out.legs.clear();

  // Already there: a valid zero-bus journey, even for an unknown stop.
  if (source == destination) {
    return true;
  }

  if (network.stop_to_routes.find(source) == network.stop_to_routes.end()) {
    return false;
  }

  const std::unordered_set<std::string> inactive(inactive_routes.begin(),
                                                 inactive_routes.end());

  std::unordered_map<std::string, int> buses_used;  // stop -> boardings
  std::unordered_map<std::string, std::string> parent_stop;
  std::unordered_map<std::string, int> parent_route;
  std::vector<char> route_expanded(network.routes.size(), 0);

  StopQueue queue(network.stop_to_routes.size());
  buses_used[source] = 0;
  queue.push(source);

  while (!queue.empty()) {
    const std::string stop = queue.pop();
    const int boarded = buses_used[stop];

    const auto routes_it = network.stop_to_routes.find(stop);
    if (routes_it == network.stop_to_routes.end()) {
      continue;
    }

    for (const int route_index : routes_it->second) {
      if (route_expanded[route_index] != 0) {
        continue;
      }
      route_expanded[route_index] = 1;

      const Route& route = network.routes[route_index];
      if (inactive.find(route.name) != inactive.end()) {
        continue;
      }

      for (const std::string& next : route.stops) {
        if (buses_used.find(next) != buses_used.end()) {
          continue;
        }

        // One more bus, no matter how many stops this route crosses.
        buses_used[next] = boarded + 1;
        parent_stop[next] = stop;
        parent_route[next] = route_index;

        if (next == destination) {
          std::string current = destination;
          while (current != source) {
            out.legs.push_back(
                {network.routes[parent_route.at(current)].name,
                 parent_stop.at(current), current});
            current = parent_stop.at(current);
          }
          std::reverse(out.legs.begin(), out.legs.end());
          return true;
        }

        queue.push(next);
      }
    }
  }

  return false;
}

}  // namespace journey
