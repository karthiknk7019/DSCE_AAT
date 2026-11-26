// -----------------------------------------------------------------------------
// server.hpp - optional browser front-end for the journey planner.
//
// `journey --serve` starts a single-threaded HTTP server on 127.0.0.1 that
// serves web/ plus a small JSON API. Every journey it returns comes from
// find_journey_stats(), i.e. the same BFS the CLI graded by make test uses.
// -----------------------------------------------------------------------------
#pragma once

#include <string>

namespace journey {

struct ServerOptions {
  std::string web_root = "web";
  std::string samples_root = "samples";
  int port = 8080;
};

// Blocks until the process is killed. Returns 1 if the socket cannot be bound.
int run_server(const ServerOptions& options);

}  // namespace journey
