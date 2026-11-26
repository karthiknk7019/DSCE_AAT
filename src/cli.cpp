// Role 2 owns this file. Do not edit it if you are Role 1 or Role 3.
#include <cctype>
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

std::string trim(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string lowercase(std::string text) {
  for (char& ch : text) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return text;
}

bool read_answer(std::ostream& out, std::istream& in, const char* prompt,
                 std::string& answer) {
  out << prompt << std::flush;
  if (!std::getline(in, answer)) {
    out << '\n';
    return false;
  }
  answer = trim(answer);
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

bool parse_network(std::istream& in, Network& network,
                   std::unordered_map<std::string, bool>& known_routes,
                   int& line_no) {
  int route_count = 0;
  if (!parse_header(in, line_no, route_count)) {
    return false;
  }
  std::vector<Route> routes;
  if (!parse_routes(in, line_no, route_count, routes, known_routes)) {
    return false;
  }
  network = build_network(routes);
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

const Route* find_route_by_name(const Network& network,
                                const std::string& name) {
  for (const Route& route : network.routes) {
    if (route.name == name) {
      return &route;
    }
  }
  return nullptr;
}

// Traces the stop sequence along `route` between `from` and `to`, picking the
// shortest slice if a stop appears more than once on the route.
std::vector<std::string> trace_leg_stops(const Route& route,
                                         const std::string& from,
                                         const std::string& to) {
  size_t best_i = 0;
  size_t best_j = 0;
  size_t best_dist = route.stops.size() + 1;

  for (size_t i = 0; i < route.stops.size(); ++i) {
    if (route.stops[i] != from) {
      continue;
    }
    for (size_t j = 0; j < route.stops.size(); ++j) {
      if (route.stops[j] != to) {
        continue;
      }
      const size_t dist = (i <= j) ? (j - i) : (i - j);
      if (dist < best_dist) {
        best_dist = dist;
        best_i = i;
        best_j = j;
      }
    }
  }

  if (best_dist > route.stops.size()) {
    return {from, to};
  }

  std::vector<std::string> path;
  path.reserve(best_dist + 1);
  if (best_i <= best_j) {
    for (size_t k = best_i; k <= best_j; ++k) {
      path.push_back(route.stops[k]);
    }
  } else {
    for (size_t k = best_i;; --k) {
      path.push_back(route.stops[k]);
      if (k == best_j) {
        break;
      }
    }
  }
  return path;
}

void print_interactive_details(std::ostream& out, const Network& network,
                               const Journey& journey) {
  if (journey.legs.empty()) {
    return;
  }

  out << "Route details:\n";
  size_t total_stops = 1;
  for (size_t i = 0; i < journey.legs.size(); ++i) {
    const Leg& leg = journey.legs[i];
    const Route* route = find_route_by_name(network, leg.route);
    std::vector<std::string> stops =
        route ? trace_leg_stops(*route, leg.from, leg.to)
              : std::vector<std::string>{leg.from, leg.to};

    if (stops.size() > 1) {
      total_stops += stops.size() - 1;
    }

    out << "  " << (i + 1) << ": " << leg.route << " (" << stops.size()
        << (stops.size() == 1 ? " stop): " : " stops): ");
    for (size_t s = 0; s < stops.size(); ++s) {
      if (s > 0) {
        out << " -> ";
      }
      out << stops[s];
    }
    out << '\n';
  }

  const size_t transfers = journey.legs.size() - 1;
  out << "Summary: " << journey.legs.size()
      << (journey.legs.size() == 1 ? " bus, " : " buses, ") << total_stops
      << " total stops, " << transfers
      << (transfers == 1 ? " transfer" : " transfers");
  if (transfers > 0) {
    out << " (change at: ";
    for (size_t i = 0; i < transfers; ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << journey.legs[i].to;
    }
    out << ')';
  }
  out << '\n';
  out.flush();
}

}  // namespace

int run_cli(std::istream& in, std::ostream& out) {
  int line_no = 0;
  Network network;
  std::unordered_map<std::string, bool> known_routes;
  if (!parse_network(in, network, known_routes, line_no)) {
    return 1;
  }

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

int run_interactive(std::istream& network_in, std::istream& input,
                    std::ostream& out) {
  Network network;
  std::unordered_map<std::string, bool> known_routes;
  int line_no = 0;
  if (!parse_network(network_in, network, known_routes, line_no)) {
    return 1;
  }

  const size_t route_count = network.routes.size();
  const size_t stop_count = network.stop_to_routes.size();
  out << "Network ready: " << route_count
      << (route_count == 1 ? " route, " : " routes, ") << stop_count
      << (stop_count == 1 ? " stop.\n" : " stops.\n");
  out << "Type 'quit' at the From prompt to exit.\n\n";

  std::string source;
  while (true) {
    if (!read_answer(out, input, "From: ", source)) {
      break;
    }
    if (source.empty()) {
      continue;
    }
    const std::string command = lowercase(source);
    if (command == "quit" || command == "exit" || command == "q") {
      break;
    }

    std::string destination;
    if (!read_answer(out, input, "To:   ", destination)) {
      break;
    }
    if (destination.empty()) {
      out << "No destination entered, starting over.\n\n";
      continue;
    }

    std::string closed_line;
    if (!read_answer(out, input,
                     "Closed routes (space separated, blank = none): ",
                     closed_line)) {
      break;
    }
    for (char& ch : closed_line) {
      if (ch == ',') {
        ch = ' ';
      }
    }

    std::vector<std::string> inactive_routes;
    std::istringstream closed(closed_line);
    std::string name;
    while (closed >> name) {
      if (known_routes.find(name) == known_routes.end()) {
        out << "WARNING: unknown route '" << name << "' ignored\n";
        continue;
      }
      inactive_routes.push_back(name);
    }

    Journey journey;
    const bool found =
        find_journey(network, source, destination, inactive_routes, journey);
    out << '\n';
    bool printed_any = false;
    print_journey(out, destination, found, journey, printed_any);
    if (found) {
      print_interactive_details(out, network, journey);
    }
    out << '\n';
  }

  return 0;
}

}  // namespace journey
