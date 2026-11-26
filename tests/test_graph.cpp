// Role 3 owns this file.
//
// These tests are written against docs/CONTRACT.md and define "done" for
// Roles 1 and 2. Until the minimum-bus BFS lands in src/graph.cpp the
// find_journey cases FAIL - that is expected. Never "fix" a failure by
// weakening a test; fix the code or explain in the PR why the test is wrong.
//
// Case numbers below match the test matrix in docs/ROLES.md -> Role 3.
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "check.hpp"
#include "journey.hpp"

namespace {

// -----------------------------------------------------------------------------
// Fixtures
// -----------------------------------------------------------------------------

journey::Network example_network() {
  return journey::build_network({
      {"R1", {"1", "2", "7"}},
      {"R2", {"3", "6", "7"}},
  });
}

// samples/demo.txt: the two-bus answer crosses more stops than the tempting
// three-bus answer, so a minimum-stops search would report 3 here.
journey::Network demo_network() {
  return journey::build_network({
      {"Outer", {"A", "S1", "S2", "S3", "S4", "S5", "M"}},
      {"Outer2", {"M", "S6", "S7", "F"}},
      {"Hop1", {"A", "B"}},
      {"Hop2", {"B", "C"}},
      {"Hop3", {"C", "F"}},
  });
}

// -----------------------------------------------------------------------------
// Assertion helpers. Each prints what failed; the file:line of the caller is
// not needed because `label` says which matrix case it is.
// -----------------------------------------------------------------------------

void expect_journey(const journey::Network& net, const std::string& source,
                    const std::string& destination,
                    const std::vector<std::string>& inactive,
                    std::size_t expected_buses, const char* label) {
  journey::Journey journey;
  if (!journey::find_journey(net, source, destination, inactive, journey)) {
    std::cerr << "FAILED [" << label << "]: no journey, expected "
              << expected_buses << " buses\n";
    ++check_failures;
    return;
  }
  if (journey.legs.size() != expected_buses) {
    std::cerr << "FAILED [" << label << "]: " << journey.legs.size()
              << " buses, expected " << expected_buses << "\n";
    ++check_failures;
  }
  if (!journey_is_valid(net, source, destination, inactive, journey)) {
    std::cerr << "FAILED [" << label << "]: journey breaks the contract\n";
    ++check_failures;
  }
}

void expect_no_journey(const journey::Network& net, const std::string& source,
                       const std::string& destination,
                       const std::vector<std::string>& inactive,
                       const char* label) {
  journey::Journey journey;
  if (journey::find_journey(net, source, destination, inactive, journey)) {
    std::cerr << "FAILED [" << label << "]: expected NO JOURNEY, got "
              << journey.legs.size() << " buses\n";
    ++check_failures;
  }
}

// -----------------------------------------------------------------------------
// Index and zero-bus cases
// -----------------------------------------------------------------------------

void test_index_maps_stops_to_routes() {
  journey::Network net = example_network();
  CHECK_EQ(net.stop_to_routes.size(), 5u);  // 1, 2, 3, 6, 7

  const std::vector<int>& at7 = net.stop_to_routes.at("7");
  CHECK_EQ(at7.size(), 2u);
  CHECK_EQ(at7[0], 0);
  CHECK_EQ(at7[1], 1);
}

// Case 3: source == destination is a successful 0-bus journey, even when the
// stop is not in the network at all (docs/CONTRACT.md section 4).
void test_same_stop_is_zero_buses() {
  journey::Network net = example_network();
  expect_journey(net, "7", "7", {}, 0u, "case 3: known stop");
  expect_journey(net, "Z9", "Z9", {}, 0u, "case 3: unknown stop");
}

// Case 1: the problem statement's worked example. R1 then R2, change at 7.
// This path is unique, so asserting the exact legs is safe here.
void test_problem_statement_example() {
  journey::Network net = example_network();
  journey::Journey j;
  const bool found = journey::find_journey(net, "1", "6", {}, j);
  CHECK_TRUE(found);
  if (!found) return;  // do not index into an empty legs vector
  CHECK_EQ(j.legs.size(), 2u);
  CHECK_EQ(j.legs[0].route, std::string("R1"));
  CHECK_EQ(j.legs[0].from, std::string("1"));
  CHECK_EQ(j.legs[0].to, std::string("7"));
  CHECK_EQ(j.legs[1].route, std::string("R2"));
  CHECK_EQ(j.legs[1].from, std::string("7"));
  CHECK_EQ(j.legs[1].to, std::string("6"));
  CHECK_TRUE(journey_is_valid(net, "1", "6", {}, j));
}

// Case 2: closing R2 removes the only link between the two halves.
void test_inactive_route_blocks_the_only_path() {
  journey::Network net = example_network();
  expect_no_journey(net, "1", "6", {"R2"}, "case 2: R2 closed");
}

// Case 4: two components that share no stop.
void test_disconnected_components() {
  journey::Network net = journey::build_network({
      {"West", {"w1", "w2", "w3"}},
      {"East", {"e1", "e2", "e3"}},
  });
  expect_no_journey(net, "w1", "e3", {}, "case 4: disconnected");
}

// Cases 5 and 6: one route, ridden forwards and "backwards". Direction is
// ignored, so both are a single bus.
void test_one_route_forward_and_backward() {
  journey::Network net = journey::build_network({
      {"Only", {"p", "q", "r"}},
  });
  expect_journey(net, "p", "r", {}, 1u, "case 5: forward");
  expect_journey(net, "r", "p", {}, 1u, "case 6: backward");
}

// Case 7: the optimum counts buses, not stops. A -> F is 2 buses via
// Outer + Outer2 (8 stops); Hop1 + Hop2 + Hop3 is only 4 stops but 3 buses.
void test_minimum_buses_not_minimum_stops() {
  journey::Network net = demo_network();
  expect_journey(net, "A", "F", {}, 2u, "case 7: min buses beats min stops");

  // Dense overlap: a direct route must still win with one bus even though
  // several multi-hop chains also connect the pair.
  journey::Network dense = journey::build_network({
      {"Direct", {"x", "y"}},
      {"Via1", {"x", "m1", "m2"}},
      {"Via2", {"m2", "m3", "y"}},
      {"Via3", {"x", "n1", "n2", "n3", "y"}},
  });
  expect_journey(dense, "x", "y", {}, 1u, "case 7: dense overlap");
}

// Case 8: closing a route the optimum does not use changes nothing.
void test_inactive_route_not_needed() {
  journey::Network net = example_network();
  expect_journey(net, "1", "2", {"R2"}, 1u, "case 8: R2 closed, R1 enough");
  expect_journey(demo_network(), "A", "F", {"Hop1", "Hop2", "Hop3"}, 2u,
                 "case 8: only Outer/Outer2 left");
}

// Case 9: unknown stops are not errors, they simply have no route.
void test_unknown_source_or_destination() {
  journey::Network net = example_network();
  expect_no_journey(net, "99", "6", {}, "case 9: unknown source");
  expect_no_journey(net, "1", "99", {}, "case 9: unknown destination");
  expect_no_journey(net, "98", "99", {}, "case 9: both unknown");
}

// Case 10: a repeated stop on one route must not invent a loop or a transfer.
void test_route_visiting_stop_twice() {
  journey::Network net = journey::build_network({
      {"Loop", {"a", "b", "a", "c"}},
  });
  expect_journey(net, "a", "c", {}, 1u, "case 10: repeated stop");
  expect_journey(net, "b", "c", {}, 1u, "case 10: after the repeat");
  expect_journey(net, "a", "a", {}, 0u, "case 10: zero buses");
}

// Case 11: the only path needs three different buses, in order.
void test_chain_of_three_buses() {
  journey::Network net = journey::build_network({
      {"A1", {"p", "q"}},
      {"A2", {"q", "r"}},
      {"A3", {"r", "s"}},
  });
  expect_journey(net, "p", "s", {}, 3u, "case 11: three buses");

  journey::Journey j;
  if (journey::find_journey(net, "p", "s", {}, j) && j.legs.size() == 3u) {
    CHECK_EQ(j.legs[0].route, std::string("A1"));
    CHECK_EQ(j.legs[1].route, std::string("A2"));
    CHECK_EQ(j.legs[2].route, std::string("A3"));
  }
}

// Case 12: k == 0 is legal. An empty route contributes no index entries and
// must not crash the build or the search.
void test_zero_stop_route() {
  journey::Network net = journey::build_network({
      {"Empty", {}},
      {"Real", {"m", "n"}},
  });
  CHECK_EQ(net.stop_to_routes.size(), 2u);  // only m and n
  expect_journey(net, "m", "n", {}, 1u, "case 12: empty route ignored");

  journey::Network empty_only = journey::build_network({{"Empty", {}}});
  expect_no_journey(empty_only, "a", "b", {}, "case 12: only empty routes");
}

// Case 13: the index must survive repeated queries. This catches a stale
// `expanded[]` flag or a mutated `dist` map between calls.
void test_repeated_queries_use_the_same_index() {
  journey::Network net = example_network();

  journey::Journey first;
  CHECK_TRUE(journey::find_journey(net, "1", "6", {}, first));
  CHECK_EQ(first.legs.size(), 2u);

  expect_no_journey(net, "1", "6", {"R2"}, "case 13: closure in between");

  journey::Journey second;
  CHECK_TRUE(journey::find_journey(net, "1", "6", {}, second));
  CHECK_EQ(second.legs.size(), 2u);
  CHECK_EQ(second.legs.size(), first.legs.size());
  if (second.legs.size() == first.legs.size()) {
    for (std::size_t i = 0; i < first.legs.size(); ++i) {
      CHECK_EQ(second.legs[i].route, first.legs[i].route);
      CHECK_EQ(second.legs[i].from, first.legs[i].from);
      CHECK_EQ(second.legs[i].to, first.legs[i].to);
    }
  }
  CHECK_TRUE(journey_is_valid(net, "1", "6", {}, second));
}

// Case 14: worst case from docs/ROLES.md - 400 routes, 40,000 appearances,
// heavy overlap, 100 queries. The 1 s limit is deliberately loose: it catches
// an accidental O(routes x stops) rescan, not machine speed.
void test_worst_case_performance() {
  const int kStopPool = 500;
  const int kRouteCount = 400;
  const int kPerRoute = 100;  // 400 * 100 = 40,000 appearances
  const int kQueries = 100;

  std::vector<journey::Route> routes;
  routes.reserve(kRouteCount);
  for (int r = 0; r < kRouteCount; ++r) {
    journey::Route route;
    route.name = "R" + std::to_string(r);
    route.stops.reserve(kPerRoute);
    const int first = (r * 7) % kStopPool;  // sliding window -> heavy overlap
    for (int i = 0; i < kPerRoute; ++i) {
      route.stops.push_back("S" + std::to_string((first + i) % kStopPool));
    }
    routes.push_back(route);
  }
  const journey::Network net = journey::build_network(routes);

  unsigned long long state = 20261126ULL;
  auto next = [&state]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<unsigned>(state >> 33);
  };

  std::size_t answered = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int q = 0; q < kQueries; ++q) {
    const std::string source = "S" + std::to_string(next() % kStopPool);
    const std::string destination = "S" + std::to_string(next() % kStopPool);
    journey::Journey journey;
    if (!journey::find_journey(net, source, destination, {}, journey)) {
      continue;
    }
    ++answered;
    if (!journey_is_valid(net, source, destination, {}, journey)) {
      std::cerr << "FAILED [case 14]: invalid journey " << source << " -> "
                << destination << "\n";
      ++check_failures;
    }
  }
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();

  // The windows overlap, so every pair is reachable: no query may be lost.
  CHECK_EQ(answered, static_cast<std::size_t>(kQueries));
  std::cout << "perf: " << kQueries << " queries on " << kRouteCount
            << " routes / " << kRouteCount * kPerRoute << " appearances in "
            << elapsed_ms << " ms (limit 1000 ms)\n";
  CHECK_TRUE(elapsed_ms < 1000);
}

}  // namespace

int main() {
  test_index_maps_stops_to_routes();
  test_same_stop_is_zero_buses();
  test_problem_statement_example();
  test_inactive_route_blocks_the_only_path();
  test_disconnected_components();
  test_one_route_forward_and_backward();
  test_minimum_buses_not_minimum_stops();
  test_inactive_route_not_needed();
  test_unknown_source_or_destination();
  test_route_visiting_stop_twice();
  test_chain_of_three_buses();
  test_zero_stop_route();
  test_repeated_queries_use_the_same_index();
  test_worst_case_performance();

  if (check_failures == 0) {
    std::cout << "All tests passed.\n";
    return 0;
  }
  std::cerr << check_failures << " check(s) failed.\n";
  return 1;
}
