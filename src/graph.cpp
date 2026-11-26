// Role 1 owns this file. Do not edit it if you are Role 2 or Role 3.
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "journey.hpp"

namespace journey {
namespace {

// Hand-written circular queue of compact stop IDs (Module 2: queue ADT).
// Capacity equals the number of distinct stops, and each stop is enqueued at
// most once per query, so the ring can never overflow.
class IntQueue {
 public:
  explicit IntQueue(std::size_t capacity)
      : data_(capacity == 0 ? 1 : capacity) {}

  bool empty() const { return size_ == 0; }

  void push(int id) {
    data_[tail_] = id;
    tail_ = (tail_ + 1) % data_.size();
    ++size_;
  }

  int pop() {
    const int id = data_[head_];
    head_ = (head_ + 1) % data_.size();
    --size_;
    return id;
  }

 private:
  std::vector<int> data_;
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
      std::vector<int>& list = network.stop_to_routes[stop];
      // A route may list a stop twice; store each route index once per stop.
      if (list.empty() || list.back() != r) {
        list.push_back(r);
      }
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

  if (network.stop_to_routes.find(source) == network.stop_to_routes.end() ||
      network.stop_to_routes.find(destination) ==
          network.stop_to_routes.end()) {
    return false;
  }

  std::unordered_map<std::string, char> inactive;
  if (!inactive_routes.empty()) {
    inactive.reserve(inactive_routes.size());
    for (const std::string& name : inactive_routes) {
      inactive[name] = 1;
    }
  }

  // Assign each visited stop a compact integer ID so the queue and parent
  // arrays avoid repeated string hashing and string copies.
  std::unordered_map<std::string, int> stop_to_id;
  const std::size_t hint =
      network.stop_to_routes.size() < 512 ? network.stop_to_routes.size() : 512;
  stop_to_id.reserve(hint);

  std::vector<const std::string*> id_to_name;
  std::vector<int> parent_stop;
  std::vector<int> parent_route;
  std::vector<char> route_expanded(network.routes.size(), 0);

  const auto src_it = stop_to_id.emplace(source, 0).first;
  id_to_name.push_back(&src_it->first);
  parent_stop.push_back(-1);
  parent_route.push_back(-1);

  IntQueue queue(network.stop_to_routes.size());
  queue.push(0);

  while (!queue.empty()) {
    const int stop_id = queue.pop();
    const std::string& stop_name = *id_to_name[static_cast<std::size_t>(stop_id)];

    const auto routes_it = network.stop_to_routes.find(stop_name);
    if (routes_it == network.stop_to_routes.end()) {
      continue;
    }

    for (const int route_index : routes_it->second) {
      if (route_expanded[route_index] != 0) {
        continue;
      }
      route_expanded[route_index] = 1;

      const Route& route = network.routes[route_index];
      if (!inactive.empty() && inactive.find(route.name) != inactive.end()) {
        continue;
      }

      for (const std::string& next : route.stops) {
        const int next_id = static_cast<int>(id_to_name.size());
        const auto [it, inserted] = stop_to_id.emplace(next, next_id);
        if (!inserted) {
          continue;
        }

        // One more bus, no matter how many stops this route crosses.
        id_to_name.push_back(&it->first);
        parent_stop.push_back(stop_id);
        parent_route.push_back(route_index);

        if (next == destination) {
          int cur = next_id;
          while (cur != 0) {
            const std::size_t cur_idx = static_cast<std::size_t>(cur);
            const int prev = parent_stop[cur_idx];
            const int r_idx = parent_route[cur_idx];
            out.legs.push_back(
                {network.routes[r_idx].name,
                 *id_to_name[static_cast<std::size_t>(prev)],
                 *id_to_name[cur_idx]});
            cur = prev;
          }
          for (std::size_t l = 0, r = out.legs.size(); l + 1 < r; ++l, --r) {
            std::swap(out.legs[l], out.legs[r - 1]);
          }
          return true;
        }

        queue.push(next_id);
      }
    }
  }

  return false;
}

}  // namespace journey
