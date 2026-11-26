// Role 1 owns this file. Do not edit it if you are Role 2 or Role 3.
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "journey.hpp"
#include "stats.hpp"

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

// 64-bit FNV-1a hash for stop names (Module 5: hashing).
std::size_t hash_stop(const std::string& s) {
  std::size_t h = 14695981039346656037ULL;
  for (unsigned char ch : s) {
    h ^= ch;
    h *= 1099511628211ULL;
  }
  return h;
}

// Hand-written open-addressing hash table mapping stop name -> compact ID.
// Keys are non-owning pointers into `network.routes` or `source`, avoiding
// string copies during BFS.
class StopIdMap {
 public:
  explicit StopIdMap(std::size_t initial_cap = 64) {
    std::size_t cap = 64;
    while (cap < initial_cap * 2) {
      cap *= 2;
    }
    slots_.resize(cap);
  }

  // Inserts `key` with `new_id` if absent; returns true if inserted.
  bool insert_if_absent(const std::string& key, int new_id) {
    if (size_ * 2 >= slots_.size()) {
      rehash(slots_.size() * 2);
    }
    const std::size_t h = hash_stop(key);
    const std::size_t mask = slots_.size() - 1;
    std::size_t idx = h & mask;
    while (slots_[idx].key != nullptr) {
      if (slots_[idx].hash == h && *slots_[idx].key == key) {
        return false;
      }
      idx = (idx + 1) & mask;
    }
    slots_[idx] = {&key, h, new_id};
    ++size_;
    return true;
  }

 private:
  struct Slot {
    const std::string* key = nullptr;
    std::size_t hash = 0;
    int id = -1;
  };

  void rehash(std::size_t new_cap) {
    std::vector<Slot> next(new_cap);
    const std::size_t mask = new_cap - 1;
    for (const Slot& s : slots_) {
      if (s.key == nullptr) {
        continue;
      }
      std::size_t idx = s.hash & mask;
      while (next[idx].key != nullptr) {
        idx = (idx + 1) & mask;
      }
      next[idx] = s;
    }
    slots_.swap(next);
  }

  std::vector<Slot> slots_;
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

namespace {

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
//
// `stats` may be null (the graded path, zero extra work); when it is non-null
// the same traversal also records the layers it discovered, which is what the
// UI uses to prove minimality.
bool search(const Network& network, const std::string& source,
            const std::string& destination,
            const std::vector<std::string>& inactive_routes, Journey& out,
            QueryStats* stats) {
  out.legs.clear();
  if (stats != nullptr) {
    *stats = QueryStats();
  }

  // Already there: a valid zero-bus journey, even for an unknown stop.
  if (source == destination) {
    if (stats != nullptr) {
      stats->reached_stops.push_back(source);
      stats->reached_buses.push_back(0);
    }
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

  // Assign each visited stop a compact integer ID using a hand-written
  // open-addressing hash table, avoiding extra hash maps and string copies.
  const std::size_t hint =
      network.stop_to_routes.size() < 512 ? network.stop_to_routes.size() : 512;
  StopIdMap visited(hint);

  std::vector<const std::string*> id_to_name;
  std::vector<int> parent_stop;
  std::vector<int> parent_route;
  std::vector<int> depth;  // buses needed to reach each visited stop
  std::vector<char> route_expanded(network.routes.size(), 0);

  visited.insert_if_absent(source, 0);
  id_to_name.push_back(&source);
  parent_stop.push_back(-1);
  parent_route.push_back(-1);
  depth.push_back(0);
  if (stats != nullptr) {
    stats->reached_stops.push_back(*id_to_name.front());
    stats->reached_buses.push_back(0);
  }

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
        if (stats != nullptr) {
          ++stats->inactive_routes_skipped;
        }
        continue;
      }

      if (stats != nullptr) {
        stats->expanded_routes.push_back(route.name);
        stats->expanded_route_buses.push_back(
            depth[static_cast<std::size_t>(stop_id)]);
      }

      for (const std::string& next : route.stops) {
        const int next_id = static_cast<int>(id_to_name.size());
        if (!visited.insert_if_absent(next, next_id)) {
          continue;
        }

        // One more bus, no matter how many stops this route crosses.
        const int next_depth = depth[static_cast<std::size_t>(stop_id)] + 1;
        id_to_name.push_back(&next);
        parent_stop.push_back(stop_id);
        parent_route.push_back(route_index);
        depth.push_back(next_depth);
        if (stats != nullptr) {
          stats->reached_stops.push_back(*id_to_name.back());
          stats->reached_buses.push_back(next_depth);
        }

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

}  // namespace

bool find_journey(const Network& network, const std::string& source,
                  const std::string& destination,
                  const std::vector<std::string>& inactive_routes,
                  Journey& out) {
  return search(network, source, destination, inactive_routes, out, nullptr);
}

bool find_journey_stats(const Network& network, const std::string& source,
                        const std::string& destination,
                        const std::vector<std::string>& inactive_routes,
                        Journey& out, QueryStats& stats) {
  return search(network, source, destination, inactive_routes, out, &stats);
}


}  // namespace journey
