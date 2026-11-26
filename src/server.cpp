// -----------------------------------------------------------------------------
// server.cpp - optional browser front-end (`journey --serve`).
//
// Plain POSIX sockets, no dependencies, so the whole demo still builds with
// `make` on any lab machine. The server is intentionally single-threaded: one
// request at a time is enough for a live demo, and it keeps the state model
// simple (the network index is built once by /api/build and reused by every
// /api/journey, exactly like the batch CLI).
//
// Endpoints (all JSON):
//   GET  /api/network   current network + summary + hubs
//   POST /api/build     {name, routes:[{name, stops:[]}]}  -> builds the index
//   POST /api/journey   {source, destination, inactive:[]} -> journey + layers
//   POST /api/impact    {source, destination, inactive:[]} -> close each route,
//                       re-query, report which closures change the answer
//   GET  /samples/<f>   raw sample input, so the UI can parse an unseen network
//
// The journey/impact answers come from find_journey_stats(), the same BFS the
// graded CLI uses; nothing here reimplements the algorithm.
// -----------------------------------------------------------------------------
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "journey.hpp"
#include "server.hpp"
#include "stats.hpp"

namespace journey {
namespace {

// -----------------------------------------------------------------------------
// Minimal JSON value + parser. The API payloads are small and fixed-shape;
// a 150-line parser keeps the project dependency-free.
// -----------------------------------------------------------------------------

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool flag = false;
  long long number = 0;
  std::string text;
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> fields;

  const Json* find(const std::string& key) const {
    for (const auto& field : fields) {
      if (field.first == key) {
        return &field.second;
      }
    }
    return nullptr;
  }
};

Json make_object() {
  Json value;
  value.type = Json::Type::Object;
  return value;
}

Json make_array() {
  Json value;
  value.type = Json::Type::Array;
  return value;
}

Json make_string(const std::string& text) {
  Json value;
  value.type = Json::Type::String;
  value.text = text;
  return value;
}

Json make_number(long long number) {
  Json value;
  value.type = Json::Type::Number;
  value.number = number;
  return value;
}

Json make_bool(bool flag) {
  Json value;
  value.type = Json::Type::Bool;
  value.flag = flag;
  return value;
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& source) : source_(source) {}

  bool parse(Json& out) {
    skip_space();
    if (!parse_value(out, 0)) {
      return false;
    }
    skip_space();
    if (pos_ != source_.size()) {
      return fail("trailing characters");
    }
    return true;
  }

  const std::string& error() const { return error_; }

 private:
  static constexpr int kMaxDepth = 64;

  bool fail(const char* message) {
    if (error_.empty()) {
      error_ = std::string(message) + " at byte " + std::to_string(pos_);
    }
    return false;
  }

  void skip_space() {
    while (pos_ < source_.size()) {
      const char ch = source_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool expect(char ch) {
    if (pos_ >= source_.size() || source_[pos_] != ch) {
      return fail("unexpected token");
    }
    ++pos_;
    return true;
  }

  bool parse_value(Json& out, int depth) {
    if (depth > kMaxDepth) {
      return fail("nesting too deep");
    }
    skip_space();
    if (pos_ >= source_.size()) {
      return fail("unexpected end of input");
    }
    switch (source_[pos_]) {
      case '{':
        return parse_object(out, depth);
      case '[':
        return parse_array(out, depth);
      case '"': {
        out.type = Json::Type::String;
        return parse_string(out.text);
      }
      case 't':
        if (source_.compare(pos_, 4, "true") == 0) {
          pos_ += 4;
          out = make_bool(true);
          return true;
        }
        return fail("bad literal");
      case 'f':
        if (source_.compare(pos_, 5, "false") == 0) {
          pos_ += 5;
          out = make_bool(false);
          return true;
        }
        return fail("bad literal");
      case 'n':
        if (source_.compare(pos_, 4, "null") == 0) {
          pos_ += 4;
          out = Json();
          return true;
        }
        return fail("bad literal");
      default:
        return parse_number(out);
    }
  }

  bool parse_number(Json& out) {
    const char* begin = source_.c_str() + pos_;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
      return fail("bad number");
    }
    pos_ += static_cast<std::size_t>(end - begin);
    out = make_number(static_cast<long long>(value));
    return true;
  }

  bool parse_string(std::string& out) {
    if (!expect('"')) {
      return false;
    }
    out.clear();
    while (pos_ < source_.size()) {
      const char ch = source_[pos_++];
      if (ch == '"') {
        return true;
      }
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      if (pos_ >= source_.size()) {
        return fail("bad escape");
      }
      const char esc = source_[pos_++];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          // Stop names are tokens; \u escapes only need to survive transport.
          if (pos_ + 4 > source_.size()) {
            return fail("bad unicode escape");
          }
          pos_ += 4;
          out.push_back('?');
          break;
        }
        default:
          return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }

  bool parse_array(Json& out, int depth) {
    out = make_array();
    if (!expect('[')) {
      return false;
    }
    skip_space();
    if (pos_ < source_.size() && source_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      Json item;
      if (!parse_value(item, depth + 1)) {
        return false;
      }
      out.items.push_back(std::move(item));
      skip_space();
      if (pos_ < source_.size() && source_[pos_] == ',') {
        ++pos_;
        continue;
      }
      return expect(']');
    }
  }

  bool parse_object(Json& out, int depth) {
    out = make_object();
    if (!expect('{')) {
      return false;
    }
    skip_space();
    if (pos_ < source_.size() && source_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      skip_space();
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_space();
      if (!expect(':')) {
        return false;
      }
      Json value;
      if (!parse_value(value, depth + 1)) {
        return false;
      }
      out.fields.emplace_back(std::move(key), std::move(value));
      skip_space();
      if (pos_ < source_.size() && source_[pos_] == ',') {
        ++pos_;
        continue;
      }
      return expect('}');
    }
  }

  const std::string& source_;
  std::size_t pos_ = 0;
  std::string error_;
};

bool parse_json(const std::string& text, Json& out, std::string& error) {
  JsonParser parser(text);
  if (!parser.parse(out)) {
    error = parser.error();
    return false;
  }
  return true;
}

void escape_json(const std::string& text, std::string& out) {
  out.push_back('"');
  for (const char ch : text) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out += "\\u00";
          const char* hex = "0123456789abcdef";
          out.push_back(hex[(ch >> 4) & 0xF]);
          out.push_back(hex[ch & 0xF]);
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
}

void dump_json(const Json& value, std::string& out) {
  switch (value.type) {
    case Json::Type::Null:
      out += "null";
      break;
    case Json::Type::Bool:
      out += value.flag ? "true" : "false";
      break;
    case Json::Type::Number:
      out += std::to_string(value.number);
      break;
    case Json::Type::String:
      escape_json(value.text, out);
      break;
    case Json::Type::Array: {
      out.push_back('[');
      for (std::size_t i = 0; i < value.items.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        dump_json(value.items[i], out);
      }
      out.push_back(']');
      break;
    }
    case Json::Type::Object: {
      out.push_back('{');
      for (std::size_t i = 0; i < value.fields.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        escape_json(value.fields[i].first, out);
        out.push_back(':');
        dump_json(value.fields[i].second, out);
      }
      out.push_back('}');
      break;
    }
  }
}

std::string dump_json(const Json& value) {
  std::string out;
  dump_json(value, out);
  return out;
}

// -----------------------------------------------------------------------------
// Application state: one network index, built once, reused by every query.
// -----------------------------------------------------------------------------

struct AppState {
  Network network;
  std::string name = "(no network)";
  int appearances = 0;
  bool loaded = false;
};

const Json* field(const Json& object, const char* key) {
  return object.find(key);
}

std::string string_field(const Json& object, const char* key,
                         const std::string& fallback = "") {
  const Json* value = field(object, key);
  if (value == nullptr || value->type != Json::Type::String) {
    return fallback;
  }
  return value->text;
}

std::vector<std::string> string_array_field(const Json& object,
                                            const char* key) {
  std::vector<std::string> out;
  const Json* value = field(object, key);
  if (value == nullptr || value->type != Json::Type::Array) {
    return out;
  }
  out.reserve(value->items.size());
  for (const Json& item : value->items) {
    if (item.type == Json::Type::String) {
      out.push_back(item.text);
    }
  }
  return out;
}

bool build_from_request(const Json& body, AppState& state, std::string& error) {
  const Json* routes_json = field(body, "routes");
  if (routes_json == nullptr || routes_json->type != Json::Type::Array) {
    error = "expected a 'routes' array";
    return false;
  }
  if (routes_json->items.size() > 400) {
    error = "too many routes (limit 400)";
    return false;
  }

  std::vector<Route> routes;
  routes.reserve(routes_json->items.size());
  std::unordered_map<std::string, bool> seen;
  long long appearances = 0;

  for (const Json& item : routes_json->items) {
    if (item.type != Json::Type::Object) {
      error = "each route must be an object";
      return false;
    }
    Route route;
    route.name = string_field(item, "name");
    if (route.name.empty()) {
      error = "route without a name";
      return false;
    }
    if (seen.find(route.name) != seen.end()) {
      error = "duplicate route name '" + route.name + "'";
      return false;
    }
    seen[route.name] = true;

    const Json* stops = field(item, "stops");
    if (stops == nullptr || stops->type != Json::Type::Array) {
      error = "route '" + route.name + "' has no stops array";
      return false;
    }
    route.stops.reserve(stops->items.size());
    for (const Json& stop : stops->items) {
      if (stop.type != Json::Type::String) {
        error = "stop names must be strings";
        return false;
      }
      route.stops.push_back(stop.text);
    }
    appearances += static_cast<long long>(route.stops.size());
    if (appearances > 40000) {
      error = "too many stop appearances (limit 40000)";
      return false;
    }
    routes.push_back(std::move(route));
  }

  // Build into a temporary so a rejected build cannot half-replace the state.
  Network network = build_network(routes);
  state.network = std::move(network);
  state.name = string_field(body, "name", "custom network");
  state.appearances = static_cast<int>(appearances);
  state.loaded = true;
  return true;
}

Json network_json(const AppState& state) {
  Json out = make_object();
  out.fields.emplace_back("name", make_string(state.name));
  out.fields.emplace_back("loaded", make_bool(state.loaded));

  Json routes = make_array();
  for (const Route& route : state.network.routes) {
    Json item = make_object();
    item.fields.emplace_back("name", make_string(route.name));
    Json stops = make_array();
    for (const std::string& stop : route.stops) {
      stops.items.push_back(make_string(stop));
    }
    item.fields.emplace_back("stops", std::move(stops));
    routes.items.push_back(std::move(item));
  }
  out.fields.emplace_back("routes", std::move(routes));

  Json summary = make_object();
  summary.fields.emplace_back(
      "routes", make_number(static_cast<long long>(state.network.routes.size())));
  summary.fields.emplace_back(
      "stops", make_number(static_cast<long long>(
                   state.network.stop_to_routes.size())));
  summary.fields.emplace_back("appearances", make_number(state.appearances));

  // Transfer hubs: stops served by the most routes. Ranking is useful in its
  // own right ("where should a closure hurt most?") and cheap to compute.
  std::vector<std::pair<int, std::string>> hubs;
  hubs.reserve(state.network.stop_to_routes.size());
  for (const auto& entry : state.network.stop_to_routes) {
    hubs.emplace_back(static_cast<int>(entry.second.size()), entry.first);
  }
  std::sort(hubs.begin(), hubs.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second < b.second;
  });
  Json hub_json = make_array();
  for (std::size_t i = 0; i < hubs.size() && i < 8; ++i) {
    Json hub = make_object();
    hub.fields.emplace_back("stop", make_string(hubs[i].second));
    hub.fields.emplace_back("routes", make_number(hubs[i].first));
    hub_json.items.push_back(std::move(hub));
  }
  summary.fields.emplace_back("hubs", std::move(hub_json));

  std::vector<std::pair<std::size_t, std::string>> largest;
  largest.reserve(state.network.routes.size());
  for (const Route& route : state.network.routes) {
    largest.emplace_back(route.stops.size(), route.name);
  }
  std::sort(largest.begin(), largest.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second < b.second;
  });
  Json largest_json = make_array();
  for (std::size_t i = 0; i < largest.size() && i < 5; ++i) {
    Json item = make_object();
    item.fields.emplace_back("name", make_string(largest[i].second));
    item.fields.emplace_back("stops",
                             make_number(static_cast<long long>(largest[i].first)));
    largest_json.items.push_back(std::move(item));
  }
  summary.fields.emplace_back("largest", std::move(largest_json));

  out.fields.emplace_back("summary", std::move(summary));
  return out;
}

// Groups the BFS discovery order into layers: how many stops and routes each
// bus count touched. This is the data behind the "why is this minimal?" panel.
Json layer_json(const QueryStats& stats) {
  std::vector<long long> stops_per_layer;
  std::vector<long long> routes_per_layer;
  for (const int buses : stats.reached_buses) {
    if (buses < 0) {
      continue;
    }
    const std::size_t index = static_cast<std::size_t>(buses);
    if (stops_per_layer.size() <= index) {
      stops_per_layer.resize(index + 1, 0);
      routes_per_layer.resize(index + 1, 0);
    }
    ++stops_per_layer[index];
  }
  for (const int buses : stats.expanded_route_buses) {
    const std::size_t index = static_cast<std::size_t>(buses);
    if (routes_per_layer.size() <= index) {
      routes_per_layer.resize(index + 1, 0);
      stops_per_layer.resize(index + 1, 0);
    }
    ++routes_per_layer[index];
  }

  Json layers = make_array();
  for (std::size_t i = 0; i < stops_per_layer.size(); ++i) {
    Json layer = make_object();
    layer.fields.emplace_back("buses",
                              make_number(static_cast<long long>(i)));
    layer.fields.emplace_back("stops", make_number(stops_per_layer[i]));
    layer.fields.emplace_back("routes", make_number(routes_per_layer[i]));
    layers.items.push_back(std::move(layer));
  }
  return layers;
}

constexpr std::size_t kMaxReachableInResponse = 2500;

Json journey_json(const AppState& state, const std::string& source,
                  const std::string& destination,
                  const std::vector<std::string>& inactive,
                  const Journey& journey, const QueryStats& stats, bool found,
                  long long microseconds) {
  Json out = make_object();
  out.fields.emplace_back("found", make_bool(found));
  out.fields.emplace_back("source", make_string(source));
  out.fields.emplace_back("destination", make_string(destination));
  out.fields.emplace_back(
      "buses", make_number(static_cast<long long>(journey.legs.size())));

  Json legs = make_array();
  for (const Leg& leg : journey.legs) {
    Json item = make_object();
    item.fields.emplace_back("route", make_string(leg.route));
    item.fields.emplace_back("from", make_string(leg.from));
    item.fields.emplace_back("to", make_string(leg.to));
    legs.items.push_back(std::move(item));
  }
  out.fields.emplace_back("legs", std::move(legs));

  Json transfers = make_array();
  for (std::size_t i = 0; i + 1 < journey.legs.size(); ++i) {
    transfers.items.push_back(make_string(journey.legs[i].to));
  }
  out.fields.emplace_back("transfers", std::move(transfers));

  Json inactive_json = make_array();
  for (const std::string& name : inactive) {
    inactive_json.items.push_back(make_string(name));
  }
  out.fields.emplace_back("inactive", std::move(inactive_json));

  Json stats_json = make_object();
  stats_json.fields.emplace_back("microseconds", make_number(microseconds));
  stats_json.fields.emplace_back(
      "routes_expanded",
      make_number(static_cast<long long>(stats.expanded_routes.size())));
  stats_json.fields.emplace_back(
      "stops_reached",
      make_number(static_cast<long long>(stats.reached_stops.size())));
  stats_json.fields.emplace_back("max_buses", make_number(stats.max_buses()));
  stats_json.fields.emplace_back(
      "inactive_skipped",
      make_number(static_cast<long long>(stats.inactive_routes_skipped)));
  stats_json.fields.emplace_back("layers", layer_json(stats));

  Json expanded = make_array();
  for (std::size_t i = 0; i < stats.expanded_routes.size() && i < 120; ++i) {
    expanded.items.push_back(make_string(stats.expanded_routes[i]));
  }
  stats_json.fields.emplace_back("expanded_routes", std::move(expanded));

  Json expanded_buses = make_array();
  for (std::size_t i = 0;
       i < stats.expanded_route_buses.size() && i < 120; ++i) {
    expanded_buses.items.push_back(
        make_number(stats.expanded_route_buses[i]));
  }
  stats_json.fields.emplace_back("expanded_route_buses",
                                 std::move(expanded_buses));

  // Reached stops power the map's BFS-layer view. Large networks are capped;
  // the layer counts above stay exact either way.
  Json reachable = make_array();
  bool truncated = false;
  for (std::size_t i = 0; i < stats.reached_stops.size(); ++i) {
    if (i >= kMaxReachableInResponse) {
      truncated = true;
      break;
    }
    Json item = make_object();
    item.fields.emplace_back("stop", make_string(stats.reached_stops[i]));
    item.fields.emplace_back("buses",
                             make_number(stats.reached_buses[i]));
    reachable.items.push_back(std::move(item));
  }
  stats_json.fields.emplace_back("reachable", std::move(reachable));
  stats_json.fields.emplace_back("reachable_truncated", make_bool(truncated));
  out.fields.emplace_back("stats", std::move(stats_json));

  out.fields.emplace_back(
      "network_routes",
      make_number(static_cast<long long>(state.network.routes.size())));
  return out;
}

Json impact_json(const AppState& state, const std::string& source,
                 const std::string& destination,
                 const std::vector<std::string>& inactive) {
  Json out = make_object();

  std::unordered_map<std::string, bool> closed;
  for (const std::string& name : inactive) {
    closed[name] = true;
  }

  Journey baseline;
  const bool baseline_found =
      find_journey(state.network, source, destination, inactive, baseline);
  out.fields.emplace_back("baseline_found", make_bool(baseline_found));
  out.fields.emplace_back(
      "baseline_buses",
      make_number(static_cast<long long>(baseline.legs.size())));

  Json rows = make_array();
  long long critical = 0;
  long long rerouted = 0;
  const long long best = static_cast<long long>(baseline.legs.size());

  for (const Route& route : state.network.routes) {
    if (closed.find(route.name) != closed.end()) {
      continue;
    }
    std::vector<std::string> trial = inactive;
    trial.push_back(route.name);

    Journey trial_journey;
    const bool found =
        find_journey(state.network, source, destination, trial, trial_journey);

    Json row = make_object();
    row.fields.emplace_back("route", make_string(route.name));
    row.fields.emplace_back("found", make_bool(found));
    row.fields.emplace_back(
        "buses", make_number(static_cast<long long>(trial_journey.legs.size())));

    long long delta = 0;
    if (!baseline_found) {
      delta = 0;
    } else if (!found) {
      delta = -1;
      ++critical;
    } else {
      delta = static_cast<long long>(trial_journey.legs.size()) - best;
      if (delta > 0) {
        ++rerouted;
      }
    }
    row.fields.emplace_back("delta", make_number(delta));
    row.fields.emplace_back("critical", make_bool(baseline_found && !found));
    rows.items.push_back(std::move(row));
  }

  out.fields.emplace_back("routes", std::move(rows));
  out.fields.emplace_back("critical_count", make_number(critical));
  out.fields.emplace_back("rerouted_count", make_number(rerouted));
  return out;
}

// -----------------------------------------------------------------------------
// HTTP plumbing
// -----------------------------------------------------------------------------

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json; charset=utf-8";
  std::string body;
};

const char* reason_phrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 431: return "Request Header Fields Too Large";
    default: return "Error";
  }
}

bool send_all(int fd, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t written =
        ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (written <= 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    sent += static_cast<std::size_t>(written);
  }
  return true;
}

void send_response(int fd, const HttpResponse& response) {
  std::ostringstream head;
  head << "HTTP/1.1 " << response.status << ' ' << reason_phrase(response.status)
       << "\r\n"
       << "Content-Type: " << response.content_type << "\r\n"
       << "Content-Length: " << response.body.size() << "\r\n"
       << "Cache-Control: no-store\r\n"
       << "X-Content-Type-Options: nosniff\r\n"
       << "Connection: close\r\n\r\n";
  send_all(fd, head.str() + response.body);
}

bool read_request(int fd, HttpRequest& request) {
  std::string buffer;
  buffer.reserve(8192);
  char chunk[16384];
  std::size_t header_end = std::string::npos;

  while (header_end == std::string::npos) {
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      return false;
    }
    buffer.append(chunk, static_cast<std::size_t>(n));
    header_end = buffer.find("\r\n\r\n");
    if (buffer.size() > 65536) {
      return false;
    }
  }

  const std::string head = buffer.substr(0, header_end);
  std::istringstream lines(head);
  std::string request_line;
  if (!std::getline(lines, request_line)) {
    return false;
  }
  if (!request_line.empty() && request_line.back() == '\r') {
    request_line.pop_back();
  }
  std::istringstream first(request_line);
  std::string version;
  if (!(first >> request.method >> request.path >> version)) {
    return false;
  }
  if (request.method != "GET" && request.method != "POST") {
    return false;
  }

  std::size_t content_length = 0;
  std::string header;
  while (std::getline(lines, header)) {
    if (!header.empty() && header.back() == '\r') {
      header.pop_back();
    }
    const std::size_t colon = header.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string name = header.substr(0, colon);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (name == "content-length") {
      const std::string value = header.substr(colon + 1);
      content_length = static_cast<std::size_t>(std::strtoull(
          value.c_str(), nullptr, 10));
    }
  }
  if (content_length > 8u * 1024u * 1024u) {
    return false;
  }

  std::string body = buffer.substr(header_end + 4);
  while (body.size() < content_length) {
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
      return false;
    }
    body.append(chunk, static_cast<std::size_t>(n));
  }
  body.resize(content_length);
  request.body = std::move(body);
  return true;
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

std::string mime_type(const std::string& path) {
  const std::size_t dot = path.find_last_of('.');
  const std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
  if (ext == "html") return "text/html; charset=utf-8";
  if (ext == "css") return "text/css; charset=utf-8";
  if (ext == "js") return "text/javascript; charset=utf-8";
  if (ext == "svg") return "image/svg+xml";
  if (ext == "json") return "application/json; charset=utf-8";
  if (ext == "png") return "image/png";
  if (ext == "woff2") return "font/woff2";
  if (ext == "ico") return "image/x-icon";
  if (ext == "txt") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

// Rejects traversal and anything outside the served root. Only characters that
// actually appear in our asset names are allowed.
bool safe_relative(const std::string& path) {
  if (path.empty() || path[0] != '/') {
    return false;
  }
  if (path.find("..") != std::string::npos ||
      path.find('\\') != std::string::npos) {
    return false;
  }
  for (const char ch : path) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '/' || ch == '.' ||
                    ch == '-' || ch == '_' || ch == ' ';
    if (!ok) {
      return false;
    }
  }
  return true;
}

void serve_static(const ServerOptions& options, const std::string& path,
                  HttpResponse& response) {
  if (!safe_relative(path)) {
    response.status = 404;
    response.body = "not found";
    response.content_type = "text/plain; charset=utf-8";
    return;
  }

  std::string relative = path;
  if (relative == "/") {
    relative = "/index.html";
  }

  std::string root = options.web_root;
  if (relative.rfind("/samples/", 0) == 0) {
    root = options.samples_root;
    // Samples are read-only data; only .txt files exist there.
    if (relative.size() < 4 || relative.substr(relative.size() - 4) != ".txt") {
      response.status = 404;
      response.body = "not found";
      response.content_type = "text/plain; charset=utf-8";
      return;
    }
    // "/samples/city.txt" -> "<samples_root>/city.txt"
    relative = relative.substr(8);
  }

  std::string body;
  const std::string full = root + relative;
  if (!read_file(full, body)) {
    response.status = 404;
    response.body = "not found: " + relative;
    response.content_type = "text/plain; charset=utf-8";
    return;
  }
  response.status = 200;
  response.content_type = mime_type(relative);
  response.body = std::move(body);
}

void api_error(HttpResponse& response, int status, const std::string& message) {
  Json error = make_object();
  error.fields.emplace_back("error", make_string(message));
  response.status = status;
  response.body = dump_json(error);
}

void handle_request(const ServerOptions& options, AppState& state,
                    const HttpRequest& request, HttpResponse& response) {
  if (request.method == "GET" && request.path == "/api/network") {
    response.body = dump_json(network_json(state));
    return;
  }

  if (request.method == "POST" && request.path == "/api/build") {
    Json body;
    std::string error;
    if (!parse_json(request.body, body, error)) {
      api_error(response, 400, "invalid JSON: " + error);
      return;
    }
    if (!build_from_request(body, state, error)) {
      api_error(response, 400, error);
      return;
    }
    response.body = dump_json(network_json(state));
    return;
  }

  const bool needs_network =
      request.path == "/api/journey" || request.path == "/api/impact";
  if (needs_network && request.method == "POST") {
    if (!state.loaded) {
      api_error(response, 409, "no network loaded; POST /api/build first");
      return;
    }
    Json body;
    std::string error;
    if (!parse_json(request.body, body, error)) {
      api_error(response, 400, "invalid JSON: " + error);
      return;
    }
    const std::string source = string_field(body, "source");
    const std::string destination = string_field(body, "destination");
    if (source.empty() || destination.empty()) {
      api_error(response, 400, "source and destination are required");
      return;
    }
    const std::vector<std::string> inactive =
        string_array_field(body, "inactive");

    if (request.path == "/api/journey") {
      Journey journey;
      QueryStats stats;
      const auto start = std::chrono::steady_clock::now();
      const bool found =
          find_journey_stats(state.network, source, destination, inactive,
                             journey, stats);
      const long long micros =
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      response.body = dump_json(journey_json(state, source, destination,
                                             inactive, journey, stats, found,
                                             micros));
      return;
    }

    const auto start = std::chrono::steady_clock::now();
    const Json impact =
        impact_json(state, source, destination, inactive);
    const long long micros =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start)
            .count();
    Json timed = impact;
    timed.fields.emplace_back("microseconds", make_number(micros));
    response.body = dump_json(timed);
    return;
  }

  if (request.method == "GET") {
    serve_static(options, request.path, response);
    return;
  }

  if (request.path.rfind("/api/", 0) == 0) {
    api_error(response, 405, "method not allowed");
    return;
  }
  api_error(response, 404, "not found");
}

}  // namespace

int run_server(const ServerOptions& options) {
  std::signal(SIGPIPE, SIG_IGN);

  const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "ERROR: cannot create socket: " << std::strerror(errno) << '\n';
    return 1;
  }

  int reuse = 1;
  ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(options.port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // localhost only

  if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
    std::cerr << "ERROR: cannot bind 127.0.0.1:" << options.port << ": "
              << std::strerror(errno) << '\n';
    ::close(server_fd);
    return 1;
  }
  if (::listen(server_fd, 16) < 0) {
    std::cerr << "ERROR: cannot listen: " << std::strerror(errno) << '\n';
    ::close(server_fd);
    return 1;
  }

  std::cout << "\n  \033[1mWayfinder\033[0m - minimum-bus journey planner\n"
            << "  http://127.0.0.1:" << options.port << "\n"
            << "  serving " << options.web_root << "/ and "
            << options.samples_root << "/  (Ctrl+C to stop)\n\n"
            << std::flush;

  AppState state;

  while (true) {
    sockaddr_in client{};
    socklen_t client_len = sizeof(client);
    const int client_fd =
        ::accept(server_fd, reinterpret_cast<sockaddr*>(&client), &client_len);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "ERROR: accept failed: " << std::strerror(errno) << '\n';
      continue;
    }

    timeval timeout{};
    timeout.tv_sec = 10;
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    HttpRequest request;
    HttpResponse response;
    if (!read_request(client_fd, request)) {
      api_error(response, 400, "malformed request");
    } else {
      handle_request(options, state, request, response);
    }
    send_response(client_fd, response);
    ::close(client_fd);
  }
}

}  // namespace journey
