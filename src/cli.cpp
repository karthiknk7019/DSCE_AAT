// Role 2 owns this file. Do not edit it if you are Role 1 or Role 3.
#include <iostream>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "journey.hpp"

namespace journey {
namespace {

constexpr int kMaxRoutes = 400;
constexpr int kMaxAppearances = 40000;

bool is_space(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
}

bool is_blank_or_comment(const std::string& line) {
  for (char ch : line) {
    if (!is_space(ch)) {
      return ch == '#';
    }
  }
  return true;
}

// Reads the next non-blank, non-comment line while keeping 1-based line numbers
// accurate for error and warning messages.
bool next_content_line(std::istream& in, std::string& line, int& line_no) {
  while (std::getline(in, line)) {
    ++line_no;
    if (!is_blank_or_comment(line)) {
      return true;
    }
  }
  return false;
}

bool parse_nonneg_int(const std::string& token, int& value, int max_val) {
  if (token.empty()) {
    return false;
  }
  long long acc = 0;
  for (char ch : token) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    acc = acc * 10 + (ch - '0');
    if (acc > max_val) {
      return false;
    }
  }
  value = static_cast<int>(acc);
  return true;
}

bool parse_header(std::istream& in, int& line_no, int& route_count) {
  std::string line;
  if (!next_content_line(in, line, line_no)) {
    std::cerr << "ERROR: missing 'routes <n>' header\n";
    return false;
  }
  std::istringstream iss(line);
  std::string keyword;
  std::string count_token;
  std::string extra;
  if (!(iss >> keyword >> count_token) || (iss >> extra) ||
      keyword != "routes" ||
      !parse_nonneg_int(count_token, route_count, kMaxRoutes)) {
    std::cerr << "ERROR: line " << line_no
              << ": expected 'routes <n>' with 0 <= n <= " << kMaxRoutes
              << '\n';
    return false;
  }
  return true;
}

bool parse_routes(std::istream& in, int& line_no, int route_count,
                  std::vector<Route>& routes,
                  std::unordered_map<std::string, bool>& known_routes) {
  routes.resize(static_cast<size_t>(route_count));
  int total_appearances = 0;

  for (int i = 0; i < route_count; ++i) {
    std::string line;
    if (!next_content_line(in, line, line_no)) {
      std::cerr << "ERROR: unexpected EOF; expected " << route_count
                << " routes, got " << i << '\n';
      return false;
    }

    std::istringstream iss(line);
    Route& route = routes[static_cast<size_t>(i)];
    std::string count_token;
    if (!(iss >> route.name >> count_token)) {
      std::cerr << "ERROR: line " << line_no
                << ": expected '<route> <k> <stop1> ... <stopk>'\n";
      return false;
    }
    if (known_routes.find(route.name) != known_routes.end()) {
      std::cerr << "ERROR: line " << line_no << ": duplicate route name '"
                << route.name << "'\n";
      return false;
    }

    int stop_count = 0;
    if (!parse_nonneg_int(count_token, stop_count, kMaxAppearances)) {
      std::cerr << "ERROR: line " << line_no << ": invalid stop count '"
                << count_token << "'\n";
      return false;
    }
    if (total_appearances + stop_count > kMaxAppearances) {
      std::cerr << "ERROR: line " << line_no
                << ": total stop appearances exceed " << kMaxAppearances
                << '\n';
      return false;
    }
    total_appearances += stop_count;

    route.stops.reserve(static_cast<size_t>(stop_count));
    std::string stop;
    while (iss >> stop) {
      route.stops.push_back(stop);
    }
    if (static_cast<int>(route.stops.size()) != stop_count) {
      std::cerr << "ERROR: line " << line_no << ": route '" << route.name
                << "' declares " << stop_count << " stops, got "
                << route.stops.size() << '\n';
      return false;
    }

    known_routes[route.name] = true;
  }
  return true;
}

void print_journey(std::ostream& out, const std::string& destination,
                   bool found, const Journey& journey, bool& printed_any) {
  // Separate consecutive blocks with a blank line, never trailing after last.
  if (printed_any) {
    out << '\n';
  }
  printed_any = true;

  if (!found) {
    out << "NO JOURNEY\n";
  } else if (journey.legs.empty()) {
    out << "JOURNEY 0 buses\n";
    out << "  already at " << destination << '\n';
  } else {
    out << "JOURNEY " << journey.legs.size() << " buses\n";
    for (size_t i = 0; i < journey.legs.size(); ++i) {
      const Leg& leg = journey.legs[i];
      out << "  " << (i + 1) << ": " << leg.route << ' ' << leg.from << " -> "
          << leg.to << '\n';
    }
  }
  out.flush();
}

}  // namespace

int run_cli(std::istream& in, std::ostream& out) {
  int line_no = 0;
  int route_count = 0;
  if (!parse_header(in, line_no, route_count)) {
    return 1;
  }

  std::vector<Route> routes;
  std::unordered_map<std::string, bool> known_routes;
  if (!parse_routes(in, line_no, route_count, routes, known_routes)) {
    return 1;
  }

  // Build the stop-to-routes index once so queries reuse it without rebuilding.
  const Network network = build_network(routes);

  bool printed_any = false;
  std::string line;
  while (next_content_line(in, line, line_no)) {
    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) {
      continue;
    }
    if (cmd == "routes") {
      std::cerr << "ERROR: line " << line_no
                << ": network already built; 'routes' may appear only once\n";
      return 1;
    }
    std::string source;
    std::string destination;
    if (cmd != "query" || !(iss >> source >> destination)) {
      std::cerr << "ERROR: line " << line_no
                << ": expected 'query <source> <destination> [inactive...]'\n";
      continue;
    }

    std::vector<std::string> inactive_routes;
    std::string inactive;
    while (iss >> inactive) {
      if (known_routes.find(inactive) == known_routes.end()) {
        std::cerr << "WARNING: line " << line_no
                  << ": unknown inactive route '" << inactive << "'\n";
        continue;
      }
      inactive_routes.push_back(inactive);
    }

    Journey journey;
    const bool found =
        find_journey(network, source, destination, inactive_routes, journey);
    print_journey(out, destination, found, journey, printed_any);
  }

  return 0;
}

}  // namespace journey
