#include <fstream>
#include <iostream>
#include <string>

#include "journey.hpp"

int main(int argc, char** argv) {
  if (argc > 1 &&
      (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    std::cout
        << "usage: journey [--interactive [network-file]]\n"
        << "  no arguments    read a network and 'query' lines from stdin\n"
        << "  --interactive   read a network, then prompt for From/To/closed\n";
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--interactive") {
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
    std::cerr << "usage: journey [--interactive [network-file]]\n";
    return 1;
  }
  return journey::run_cli(std::cin, std::cout);
}
