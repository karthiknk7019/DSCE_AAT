// Role 2 owns this file. Do not edit it if you are Role 1 or Role 3.
#include <istream>
#include <ostream>
#include <string>

#include "journey.hpp"

namespace journey {

// -----------------------------------------------------------------------------
// TODO(role-2): implement the command-line loop.
//
// Grammar (docs/CONTRACT.md):
//   routes <n>
//   <route> <k> <stop1> ... <stopk>        (repeat n times)
//   query <source> <destination> [inactive route ...]
//   query ...
//
// Suggested plan:
//   1. read the `routes` block once, then call build_network ONCE
//      (this is what makes repeated queries cheap)
//   2. loop over `query` lines and call find_journey
//   3. print exactly the three output forms in docs/CONTRACT.md
//   4. ignore blank lines and full-line '#' comments
//   5. malformed lines go to std::cerr; keep serving later queries
//
// Return 1 only when the network itself is malformed; NO JOURNEY is not an
// error and must still exit 0.
// -----------------------------------------------------------------------------
int run_cli(std::istream& in, std::ostream& out) {
  (void)in;
  (void)out;
  return 0;
}

}  // namespace journey
