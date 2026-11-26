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

  // Inserts `key` with `new_id` if absent; returns its ID and whether it was
  // newly inserted.
  std::pair<int, bool> insert_or_get(const std::string& key, int new_id) {
    if (size_ * 2 >= slots_.size()) {
      rehash(slots_.size() * 2);
    }
    const std::size_t h = hash_stop(key);
    const std::size_t mask = slots_.size() - 1;
    std::size_t idx = h & mask;
    while (slots_[idx].key != nullptr) {
      if (slots_[idx].hash == h && *slots_[idx].key == key) {
        return {slots_[idx].id, false};
      }
      idx = (idx + 1) & mask;
    }
    slots_[idx] = {&key, h, new_id};
    ++size_;
    return {new_id, true};
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

// In-place merge sort for small layer vectors (avoids <algorithm> dependency).
template <typename Less>
void sort_layer(std::vector<int>& items, std::vector<int>& scratch,
                std::size_t left, std::size_t right, Less less) {
  if (right - left <= 1) {
    return;
  }
  const std::size_t mid = left + (right - left) / 2;
  sort_layer(items, scratch, left, mid, less);
  sort_layer(items, scratch, mid, right, less);

  std::size_t i = left;
  std::size_t j = mid;
  std::size_t k = left;
  while (i < mid && j < right) {
    if (less(items[j], items[i])) {
      scratch[k++] = items[j++];
    } else {
      scratch[k++] = items[i++];
    }
  }
  while (i < mid) {
    scratch[k++] = items[i++];
  }
  while (j < right) {
    scratch[k++] = items[j++];
  }
  for (std::size_t p = left; p < right; ++p) {
    items[p] = scratch[p];
  }
}

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
  std::vector<int> stop_layer;
  std::vector<int> stop_rank;
  std::vector<int> parent_stop;
  std::vector<int> parent_route;
  std::vector<char> route_expanded(network.routes.size(), 0);

  visited.insert_or_get(source, 0);
  id_to_name.push_back(&source);
  stop_layer.push_back(0);
  stop_rank.push_back(0);
  parent_stop.push_back(-1);
  parent_route.push_back(-1);
  if (stats != nullptr) {
    stats->reached_stops.push_back(*id_to_name.front());
    stats->reached_buses.push_back(0);
  }

  IntQueue queue(network.stop_to_routes.size());
  queue.push(0);

  int dst_id = -1;
  int current_layer_num = 0;
  std::size_t layer_count = 1;
  std::vector<int> next_layer;
  std::vector<int> sort_scratch;

  while (!queue.empty()) {
    next_layer.clear();

    // Process the entire current boarding layer in ascending prefix-rank order
    // so ties always prefer the lexicographically smallest route sequence.
    for (std::size_t q_i = 0; q_i < layer_count; ++q_i) {
      const int stop_id = queue.pop();
      const std::string& stop_name =
          *id_to_name[static_cast<std::size_t>(stop_id)];

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
          stats->expanded_route_buses.push_back(current_layer_num);
        }

        for (const std::string& next : route.stops) {
          const int candidate_id = static_cast<int>(id_to_name.size());
          const auto [next_id, inserted] =
              visited.insert_or_get(next, candidate_id);
          const std::size_t next_idx = static_cast<std::size_t>(next_id);

          if (inserted) {
            id_to_name.push_back(&next);
            stop_layer.push_back(current_layer_num + 1);
            stop_rank.push_back(0);
            parent_stop.push_back(stop_id);
            parent_route.push_back(route_index);
            next_layer.push_back(next_id);
            if (stats != nullptr) {
              stats->reached_stops.push_back(*id_to_name.back());
              stats->reached_buses.push_back(current_layer_num + 1);
            }
            if (next == destination) {
              dst_id = next_id;
            }
          } else if (stop_layer[next_idx] == current_layer_num + 1) {
            // Same boarding count: keep the lexicographically smaller prefix.
            const int cur_rank =
                stop_rank[static_cast<std::size_t>(parent_stop[next_idx])];
            const int new_rank =
                stop_rank[static_cast<std::size_t>(stop_id)];
            const std::string& cur_rname =
                network.routes[parent_route[next_idx]].name;
            if (new_rank < cur_rank ||
                (new_rank == cur_rank && route.name < cur_rname)) {
              parent_stop[next_idx] = stop_id;
              parent_route[next_idx] = route_index;
            }
          }
        }
      }
    }

    if (dst_id != -1) {
      int cur = dst_id;
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

    if (next_layer.empty()) {
      break;
    }

    auto lex_less = [&](int a, int b) {
      const std::size_t ai = static_cast<std::size_t>(a);
      const std::size_t bi = static_cast<std::size_t>(b);
      const int ra = stop_rank[static_cast<std::size_t>(parent_stop[ai])];
      const int rb = stop_rank[static_cast<std::size_t>(parent_stop[bi])];
      if (ra != rb) {
        return ra < rb;
      }
      return network.routes[parent_route[ai]].name <
             network.routes[parent_route[bi]].name;
    };

    sort_scratch.resize(next_layer.size());
    sort_layer(next_layer, sort_scratch, 0, next_layer.size(), lex_less);

    int rank = 0;
    for (std::size_t i = 0; i < next_layer.size(); ++i) {
      if (i > 0 && lex_less(next_layer[i - 1], next_layer[i])) {
        ++rank;
      }
      stop_rank[static_cast<std::size_t>(next_layer[i])] = rank;
      queue.push(next_layer[i]);
    }

    layer_count = next_layer.size();
    ++current_layer_num;
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
