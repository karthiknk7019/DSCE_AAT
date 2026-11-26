// Role 3 owns this file.
//
// These tests are written against docs/CONTRACT.md. The find_journey tests
// FAIL until Role 1 implements the BFS - that is expected. Never "fix" a
// failure by weakening a test; fix the code.
//
// TODO(role-3) add at least these cases (the problem statement requires
// disconnected and heavily overlapping networks):
//   [ ] disconnected network -> false
//   [ ] one route, source before destination -> 1 bus
//   [ ] one route, riding "backwards" -> 1 bus (direction is ignored)
//   [ ] source == destination -> 0 buses, even if the stop is unknown
//   [ ] unknown source or destination -> false
//   [ ] inactive route that is not needed -> journey unchanged
//   [ ] inactive route that is the only option -> false
//   [ ] three buses when no two-bus journey exists
//   [ ] overlapping routes: 3 buses, assert the result is still 3, not 4
//   [ ] a route that visits a stop twice does not create a fake loop
//   [ ] repeated queries on one Network give identical answers
//   [ ] large generated network (400 routes / 40k appearances) under 1 s
#include <string>
#include <vector>

#include "check.hpp"
#include "journey.hpp"

namespace {

journey::Network example_network() {
  return journey::build_network({
      {"R1", {"1", "2", "7"}},
      {"R2", {"3", "6", "7"}},
  });
}

void test_index_maps_stops_to_routes() {
  journey::Network net = example_network();
  CHECK_EQ(net.stop_to_routes.size(), 5u);  // 1, 2, 3, 6, 7

  const std::vector<int>& at7 = net.stop_to_routes.at("7");
  CHECK_EQ(at7.size(), 2u);
  CHECK_EQ(at7[0], 0);
  CHECK_EQ(at7[1], 1);
}

void test_same_stop_is_zero_buses() {
  journey::Network net = example_network();
  journey::Journey j;
  CHECK_TRUE(journey::find_journey(net, "7", "7", {}, j));
  CHECK_EQ(j.legs.size(), 0u);
}

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
}

void test_inactive_route_blocks_the_only_path() {
  journey::Network net = example_network();
  journey::Journey j;
  CHECK_TRUE(!journey::find_journey(net, "1", "6", {"R2"}, j));
}

}  // namespace

int main() {
  test_index_maps_stops_to_routes();
  test_same_stop_is_zero_buses();
  test_problem_statement_example();
  test_inactive_route_blocks_the_only_path();

  if (check_failures == 0) {
    std::cout << "All tests passed.\n";
    return 0;
  }
  std::cerr << check_failures << " check(s) failed.\n";
  return 1;
}
