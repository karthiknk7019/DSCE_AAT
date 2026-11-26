#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "journey.hpp"
#include "server.hpp"

namespace {

void print_usage() {
  std::cout
      << "usage: journey [--interactive [network-file]]\n"
      << "       journey --serve [--port N] [--web DIR]\n"
      << "  no arguments    read a network and 'query' lines from stdin\n"
      << "  --interactive   read a network, then prompt for From/To/closed\n"
      << "  --serve         start the browser UI (default port 8080)\n";
}

bool parse_port(const std::string& text, int& port) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || value < 1 || value > 65535) {
    return false;
  }
  port = static_cast<int>(value);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string first = argc > 1 ? argv[1] : "";

  if (first == "--help" || first == "-h") {
    print_usage();
    return 0;
  }

  if (first == "--serve") {
    journey::ServerOptions options;
    for (int i = 2; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--port" && i + 1 < argc) {
        if (!parse_port(argv[++i], options.port)) {
          std::cerr << "ERROR: invalid port '" << argv[i] << "'\n";
          return 1;
        }
      } else if (arg == "--web" && i + 1 < argc) {
        options.web_root = argv[++i];
      } else {
        std::cerr << "usage: journey --serve [--port N] [--web DIR]\n";
        return 1;
      }
    }
    return journey::run_server(options);
  }

  if (first == "--interactive") {
    if (argc > 3) {
      std::cerr << "usage: journey [--interactive [network-file]]\n";
      return 1;
    }
    std::ifstream file;
    std::istream* network_in = &std::cin;
    if (argc == 3) {
      file.open(argv[2]);
      if (!file) {
        std::cerr << "ERROR: cannot open '" << argv[2] << "'\n";
        return 1;
      }
      network_in = &file;
    }
    return journey::run_interactive(*network_in, std::cin, std::cout);
  }
  if (argc > 1) {
    print_usage();
    return 1;
  }
  return journey::run_cli(std::cin, std::cout);
}
