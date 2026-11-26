// Terminal explainer: prints the BFS by boardings for one query.
//
// This is a demo tool, not the graded path. It parses the contract input,
// builds the real index with build_network(), and calls the real
// find_journey_stats() -- so the layers below come from src/graph.cpp.
//
//   ./build/explain <network-file> <from> <to> [closed-route ...]
//
// Role 2 owns src/cli.cpp, but this file lives in tools/ and only reads the
// frozen interface; it never changes the planner.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "journey.hpp"
#include "stats.hpp"

namespace {

struct Query {
  std::string source;
  std::string destination;
  std::vector<std::string> inactive_routes;
};

struct Parsed {
  journey::Network network;
  std::size_t appearances = 0;
};

bool parse(std::istream& in, Parsed& out) {
  std::string line;
  bool header_seen = false;
  int routes_left = 0;
  int line_no = 0;

  while (std::getline(in, line)) {
    ++line_no;
    std::istringstream ls(line);
    std::string first;
    if (!(ls >> first) || first[0] == '#') {
      continue;
    }
    if (!header_seen) {
      if (first != "routes") {
        std::cerr << "line " << line_no << ": expected 'routes <n>'\n";
        return false;
      }
      if (!(ls >> routes_left) || routes_left < 0) {
        std::cerr << "line " << line_no << ": bad route count\n";
        return false;
      }
      header_seen = true;
      continue;
    }
    if (routes_left > 0) {
      journey::Route route;
      int k = 0;
      if (!(ls >> k) || k < 0) {
        std::cerr << "line " << line_no << ": bad stop count\n";
        return false;
      }
      route.name = first;
      route.stops.reserve(static_cast<std::size_t>(k));
      for (int i = 0; i < k; ++i) {
        std::string stop;
        if (!(ls >> stop)) {
          std::cerr << "line " << line_no << ": route too short\n";
          return false;
        }
        route.stops.push_back(stop);
      }
      out.appearances += route.stops.size();
      out.network.routes.push_back(std::move(route));
      --routes_left;
      if (routes_left == 0) {
        out.network = journey::build_network(out.network.routes);
      }
      continue;
    }
    if (first == "query") {
      // Queries in the file are ignored; this tool takes one from argv.
      continue;
    }
  }
  if (!header_seen || routes_left != 0) {
    std::cerr << "bad network: missing header or too few routes\n";
    return false;
  }
  return true;
}

// Print at most `limit` items, then "+N more".
void print_list(const std::vector<std::string>& items, std::size_t limit,
                const char* sep) {
  const std::size_t shown = items.size() < limit ? items.size() : limit;
  for (std::size_t i = 0; i < shown; ++i) {
    if (i > 0) {
      std::cout << sep;
    }
    std::cout << items[i];
  }
  if (items.size() > shown) {
    std::cout << sep << "+" << (items.size() - shown) << " more";
  }
}

// The BFS stores leg endpoints; a rider actually travels every stop between
// them. Pick the shortest slice of the route (routes are undirected and a
// stop may repeat), exactly as the interactive mode does.
std::vector<std::string> trace_leg(const journey::Route& route,
                                   const std::string& from,
                                   const std::string& to) {
  std::size_t best_i = 0;
  std::size_t best_j = 0;
  std::size_t best = route.stops.size() + 1;
  for (std::size_t i = 0; i < route.stops.size(); ++i) {
    if (route.stops[i] != from) {
      continue;
    }
    for (std::size_t j = 0; j < route.stops.size(); ++j) {
      if (route.stops[j] != to) {
        continue;
      }
      const std::size_t dist = i <= j ? j - i : i - j;
      if (dist < best) {
        best = dist;
        best_i = i;
        best_j = j;
      }
    }
  }
  if (best > route.stops.size()) {
    return {from, to};
  }
  std::vector<std::string> path;
  if (best_i <= best_j) {
    for (std::size_t k = best_i; k <= best_j; ++k) {
      path.push_back(route.stops[k]);
    }
  } else {
    for (std::size_t k = best_i;; --k) {
      path.push_back(route.stops[k]);
      if (k == best_j) {
        break;
      }
    }
  }
  return path;
}

void explain(const Parsed& p, const Query& q) {
  journey::Journey journey;
  journey::QueryStats stats;
  const bool found = journey::find_journey_stats(
      p.network, q.source, q.destination, q.inactive_routes, journey, stats);

  std::size_t max_buses = 0;
  for (int b : stats.reached_buses) {
    if (b > 0 && static_cast<std::size_t>(b) > max_buses) {
      max_buses = static_cast<std::size_t>(b);
    }
  }

  std::cout << "\nquery: " << q.source << " -> " << q.destination;
  if (!q.inactive_routes.empty()) {
    std::cout << "   (closed:";
    for (const std::string& r : q.inactive_routes) {
      std::cout << ' ' << r;
    }
    std::cout << ')';
  }
  std::cout << "\n\n";
  std::cout << "BFS by boardings (one layer = one bus)\n";

  for (std::size_t bus = 0; bus <= max_buses; ++bus) {
    // Routes boarded while standing in this layer discover the next layer.
    std::vector<std::string> boarded;
    for (std::size_t i = 0; i < stats.expanded_routes.size(); ++i) {
      if (static_cast<std::size_t>(stats.expanded_route_buses[i]) == bus) {
        boarded.push_back(stats.expanded_routes[i]);
      }
    }

    std::vector<std::string> reached;
    for (std::size_t i = 0; i < stats.reached_stops.size(); ++i) {
      if (static_cast<std::size_t>(stats.reached_buses[i]) == bus) {
        reached.push_back(stats.reached_stops[i]);
      }
    }

    std::cout << "\n  layer " << bus << "  " << bus
              << (bus == 1 ? " bus" : " buses") << "  (" << reached.size()
              << (reached.size() == 1 ? " stop" : " stops") << ")\n";
    if (reached.size() <= 12) {
      std::cout << "    stops   : ";
      print_list(reached, reached.size(), ", ");
      std::cout << "\n";
    }
    if (!boarded.empty()) {
      std::cout << "    board   : ";
      if (boarded.size() <= 8) {
        print_list(boarded, boarded.size(), ", ");
      } else {
        std::cout << boarded.size() << " routes";
      }
      std::cout << "\n";
    }
    bool has_destination = false;
    for (const std::string& stop : reached) {
      if (stop == q.destination) {
        has_destination = true;
      }
    }
    if (has_destination) {
      std::cout << "    destination: " << q.destination
                << " first reached here -> minimum " << bus
                << (bus == 1 ? " bus\n" : " buses\n");
    }
  }

  if (!found) {
    std::cout << "\nNO JOURNEY (the active component is exhausted)\n";
  } else {
    std::cout << "\nminimum: " << journey.legs.size()
              << (journey.legs.size() == 1 ? " bus\n" : " buses\n");
    for (std::size_t i = 0; i < journey.legs.size(); ++i) {
      const journey::Leg& leg = journey.legs[i];
      std::cout << "  " << (i + 1) << ". " << leg.route << "  " << leg.from
                << " -> " << leg.to << "\n";

      const journey::Route* route = nullptr;
      for (const journey::Route& r : p.network.routes) {
        if (r.name == leg.route) {
          route = &r;
          break;
        }
      }
      const std::vector<std::string> stops =
          route != nullptr ? trace_leg(*route, leg.from, leg.to)
                           : std::vector<std::string>{leg.from, leg.to};
      std::cout << "      " << (stops.size() - 1) << " stops after boarding: ";
      int per_line = 0;
      for (std::size_t s = 1; s < stops.size(); ++s) {
        if (s > 1) {
          std::cout << " -> ";
        }
        std::cout << stops[s];
        if (++per_line % 6 == 0 && s + 1 < stops.size()) {
          std::cout << "\n        ";
        }
      }
      std::cout << "\n";
    }
    if (journey.legs.size() > 1) {
      std::cout << "  change at:";
      for (std::size_t i = 0; i + 1 < journey.legs.size(); ++i) {
        std::cout << ' ' << journey.legs[i].to;
      }
      std::cout << "\n";
    }
  }

  std::cout << "\nexpanded " << stats.expanded_routes.size() << " routes, "
            << stats.reached_stops.size() << " stops reached";
  if (stats.inactive_routes_skipped > 0) {
    std::cout << ", " << stats.inactive_routes_skipped
              << " closed-route expansions skipped";
  }
  std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string network_path = "samples/bmtc.txt";
  int argi = 1;
  if (argc > 1 && argv[1][0] != '-') {
    network_path = argv[1];
    argi = 2;
  }

  Query q;
  bool asked = false;
  if (argc > argi) {
    q.source = argv[argi++];
  } else {
    std::cout << "From: " << std::flush;
    std::getline(std::cin, q.source);
    asked = true;
  }
  if (argc > argi) {
    q.destination = argv[argi++];
  } else {
    std::cout << "To:   " << std::flush;
    std::getline(std::cin, q.destination);
    asked = true;
  }
  if (q.source.empty() || q.destination.empty()) {
    std::cerr << "need a source and a destination\n";
    return 2;
  }

  // The map shows pretty names ("Kempegowda Bus Station (Majestic/KBS)");
  // the contract uses tokens. Accept both by trimming and joining words.
  auto normalize = [](std::string s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) {
      return std::string();
    }
    s = s.substr(b, e - b + 1);
    std::string out;
    for (char c : s) {
      if (c == ' ' || c == '\t') {
        c = '_';
      }
      if (c == '_' && !out.empty() && out.back() == '_') {
        continue;
      }
      out += c;
    }
    // "Station_(Majestic" -> "Station(Majestic" (the GTFS token style).
    const std::size_t paren = out.find("_(");
    if (paren != std::string::npos) {
      out.erase(paren, 1);
    }
    return out;
  };
  q.source = normalize(q.source);
  q.destination = normalize(q.destination);
  if (argc > argi) {
    for (; argi < argc; ++argi) {
      q.inactive_routes.push_back(argv[argi]);
    }
  } else if (asked) {
    std::cout << "Closed routes (space separated, blank = none): "
              << std::flush;
    std::string line;
    std::getline(std::cin, line);
    std::istringstream ls(line);
    std::string closed;
    while (ls >> closed) {
      q.inactive_routes.push_back(closed);
    }
  }

  std::ifstream file(network_path);
  if (!file) {
    std::cerr << "cannot open " << network_path << "\n";
    return 2;
  }

  Parsed parsed;
  if (!parse(file, parsed)) {
    return 1;
  }

  const auto& index = parsed.network.stop_to_routes;
  if (index.find(q.source) == index.end() ||
      index.find(q.destination) == index.end()) {
    std::cerr << "stop not found in " << network_path << ":\n";
    if (index.find(q.source) == index.end()) {
      std::cerr << "  from: " << q.source << "\n";
    }
    if (index.find(q.destination) == index.end()) {
      std::cerr << "  to:   " << q.destination << "\n";
    }
    std::cerr << "Use the tokens from " << network_path
              << " (spaces become _).\n";
    return 2;
  }

  std::cout << "\nWayfinder - minimum-bus trace\n";
  std::cout << "network: " << network_path << "  ("
            << parsed.network.routes.size() << " routes, "
            << parsed.network.stop_to_routes.size() << " stops)\n";

  explain(parsed, q);
  return 0;
}
