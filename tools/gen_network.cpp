// Role 3 owns this file.
//
// Generates a worst-case input in the CLI format of docs/CONTRACT.md:
// 400 routes, 40,000 stop appearances, heavy overlap, 100 queries.
//
// Deliberately deterministic: the same bytes every run, so a slow perf number
// can be reproduced. A hand-written 64-bit LCG is used instead of <random> so
// the tool stays inside the project's small allowed toolbox and does not
// depend on the standard library's distribution implementations.
//
// Usage: ./build/gen_network > samples/big.txt   (or `make gen`)
#include <iostream>
#include <string>

namespace {

constexpr int kStopPool = 500;      // distinct stops
constexpr int kRouteCount = 400;    // contract maximum
constexpr int kPerRoute = 100;      // 400 * 100 = 40,000 appearances
constexpr int kQueryCount = 100;
constexpr int kInactivePerQuery = 3;

std::string stop_name(int index) { return "S" + std::to_string(index); }

// Numerical Recipes LCG; only the high bits are returned because they are the
// well-mixed ones.
class Lcg {
 public:
  explicit Lcg(unsigned long long seed) : state_(seed) {}

  unsigned next() {
    state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<unsigned>(state_ >> 33);
  }

 private:
  unsigned long long state_;
};

}  // namespace

int main() {
  std::cout << "routes " << kRouteCount << '\n';

  // Each route is a sliding window over the stop pool. Consecutive routes share
  // almost all of their stops, which is the overlap that punishes a search
  // which rescans a route from every stop that reaches it.
  for (int r = 0; r < kRouteCount; ++r) {
    std::cout << 'R' << r << ' ' << kPerRoute;
    const int first = (r * 7) % kStopPool;
    for (int i = 0; i < kPerRoute; ++i) {
      std::cout << ' ' << stop_name((first + i) % kStopPool);
    }
    std::cout << '\n';
  }

  Lcg rng(20261126ULL);
  std::cout << '\n';
  for (int q = 0; q < kQueryCount; ++q) {
    const int source = static_cast<int>(rng.next() % kStopPool);
    const int destination = static_cast<int>(rng.next() % kStopPool);
    std::cout << "query " << stop_name(source) << ' '
              << stop_name(destination);
    // A few closures per query exercise the per-query inactive filter.
    for (int i = 0; i < kInactivePerQuery; ++i) {
      std::cout << " R" << (rng.next() % kRouteCount);
    }
    std::cout << '\n';
  }

  return 0;
}
