#!/usr/bin/env python3
"""Build a contract-format BMTC sample plus coordinates for the UI map.

Data: Vonter/bmtc-gtfs (unofficial BMTC GTFS, ODbL 1.0)
      https://github.com/Vonter/bmtc-gtfs

The source files are large (routes.csv ~7 MB, stops.geojson ~27 MB). This
script reads them locally if present, otherwise downloads them with urllib.
Only the standard library is used: this is a build-time tool, not part of the
graded C++ program.

Outputs:
  samples/bmtc.txt      <=400 routes / <=40,000 appearances, contract grammar
  web/data/bmtc.json    stop coordinates + pretty names + attribution
  samples/expected-bmtc.txt  the CLI output for the sample's queries

Usage:
  python3 tools/import_bmtc.py
  python3 tools/import_bmtc.py --limit 300 --max-appearances 30000
"""

from __future__ import annotations

import argparse
import ast
import csv
import json
import math
import re
import urllib.request
from collections import defaultdict
from pathlib import Path

ROUTES_URL = (
    "https://raw.githubusercontent.com/Vonter/bmtc-gtfs/main/csv/routes.csv"
)
STOPS_URL = (
    "https://raw.githubusercontent.com/Vonter/bmtc-gtfs/main/geojson/stops.geojson"
)
SOURCE = "https://github.com/Vonter/bmtc-gtfs"
LICENSE = "ODbL 1.0"
CITY = "Bengaluru"
CENTER = [77.5946, 12.9716]

LANDMARKS = [
    ("Jayanagar", "Hebbal"),
    ("Kempegowda Bus Station", "Whitefield"),
    ("Kempegowda Bus Station", "Kempegowda International Airport"),
    ("Banashankari", "Marathahalli"),
    ("Kempegowda Bus Station", "Electronic City"),
    ("Kempegowda Bus Station", "Yeshwanthpur"),
]


def tokenize(name: str) -> str:
    """One whitespace-free token, as docs/CONTRACT.md requires."""
    text = re.sub(r"\s+", "_", name.strip())
    text = text.replace('"', "").replace("\\", "")
    text = re.sub(r"_+", "_", text).strip("_")
    if not text:
        return "unnamed"
    if text.startswith("#"):
        text = "S" + text
    return text


def normalized(name: str) -> str:
    return re.sub(r"[\s_]+", " ", name.strip()).lower()


def download(url: str, path: Path) -> Path:
    if path.exists() and path.stat().st_size > 1000:
        return path
    print(f"downloading {url} -> {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(url, path)
    return path


def load_stop_coordinates(path: Path) -> dict[str, list[float]]:
    data = json.loads(path.read_text())
    coordinates: dict[str, list[float]] = {}
    for feature in data["features"]:
        name = feature["properties"].get("name", "")
        geometry = feature.get("geometry") or {}
        if geometry.get("type") != "Point" or not name:
            continue
        lon, lat = geometry["coordinates"][:2]
        key = normalized(name)
        # Stops with the same display name exist on both sides of a road.
        # Averaging is better than picking one arbitrary side for a map.
        if key in coordinates:
            old = coordinates[key]
            coordinates[key] = [(old[0] + lon) / 2, (old[1] + lat) / 2]
        else:
            coordinates[key] = [lon, lat]
    return coordinates


def parse_stop_list(raw: str) -> list[str]:
    try:
        value = ast.literal_eval(raw)
    except (SyntaxError, ValueError):
        return []
    if not isinstance(value, list):
        return []
    return [str(item).strip() for item in value if str(item).strip()]


def route_candidates(
    routes_path: Path, coordinates: dict[str, list[float]]
) -> tuple[list[dict], dict[str, int]]:
    """One direction per route number, preferring the longest pattern."""
    by_name: dict[str, dict] = {}
    trips_by_name: dict[str, int] = defaultdict(int)

    with routes_path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            name = (row.get("name") or "").strip()
            if not name:
                continue
            trips = int(row.get("trip_count") or 0)
            trips_by_name[name] += trips
            stops = parse_stop_list(row.get("stop_list") or "")
            if len(stops) < 5:
                continue
            if any(normalized(stop) not in coordinates for stop in stops):
                continue
            candidate = {
                "name": name,
                "trip_count": trips,
                "stops": stops,
            }
            current = by_name.get(name)
            if current is None or (
                len(stops),
                trips,
            ) > (
                len(current["stops"]),
                current["trip_count"],
            ):
                by_name[name] = candidate

    return list(by_name.values()), trips_by_name


def select_routes(
    candidates: list[dict], trips_by_name: dict[str, int], limit: int,
    max_appearances: int,
) -> list[dict]:
    ranked = sorted(
        candidates,
        key=lambda route: (-trips_by_name.get(route["name"], 0), route["name"]),
    )
    selected: list[dict] = []
    total = 0
    for route in ranked:
        if len(selected) >= limit:
            break
        count = len(route["stops"])
        if selected and total + count > max_appearances:
            continue
        selected.append(route)
        total += count
    return selected


def build_index(routes: list[dict]) -> tuple[dict[str, set[str]], dict[str, int]]:
    stop_routes: dict[str, set[str]] = defaultdict(set)
    for route in routes:
        for stop in route["stops"]:
            stop_routes[stop].add(route["name"])
    return stop_routes, {stop: len(names) for stop, names in stop_routes.items()}


def min_buses(
    stop_routes: dict[str, set[str]], route_stops: dict[str, list[str]],
    source: str, destination: str,
) -> int | None:
    if source == destination:
        return 0
    if source not in stop_routes or destination not in stop_routes:
        return None
    seen = {source}
    expanded: set[str] = set()
    frontier = [source]
    depth = 0
    while frontier:
        depth += 1
        next_frontier = []
        for stop in frontier:
            for route_name in stop_routes[stop]:
                if route_name in expanded:
                    continue
                expanded.add(route_name)
                for neighbour in route_stops[route_name]:
                    if neighbour in seen:
                        continue
                    if neighbour == destination:
                        return depth
                    seen.add(neighbour)
                    next_frontier.append(neighbour)
        frontier = next_frontier
    return None


def find_stop(stop_index: dict[str, int], needle: str) -> str | None:
    target = normalized(needle)
    exact = [stop for stop in stop_index if normalized(stop) == target]
    if exact:
        return exact[0]
    partial = [stop for stop in stop_index if target in normalized(stop)]
    return sorted(partial)[0] if partial else None


def choose_queries(
    routes: list[dict], stop_routes: dict[str, set[str]],
) -> list[tuple[str, str, int]]:
    route_stops = {route["name"]: route["stops"] for route in routes}
    stop_index = {stop: 1 for stop in stop_routes}
    queries: list[tuple[str, str, int]] = []
    used: set[tuple[str, str]] = set()

    for left, right in LANDMARKS:
        source = find_stop(stop_index, left)
        destination = find_stop(stop_index, right)
        if not source or not destination or source == destination:
            continue
        key = (source, destination)
        if key in used:
            continue
        count = min_buses(stop_routes, route_stops, source, destination)
        if count is None:
            continue
        used.add(key)
        queries.append((source, destination, count))
        if len(queries) >= 4:
            break

    if len(queries) < 3 and stop_index:
        hub = max(stop_index, key=lambda stop: len(stop_routes[stop]))
        # Farthest served stop from the hub (the map's coordinates are only
        # in the JSON, so use the stop name list as a fallback: first stop
        # that is not the hub and produces a journey).
        for stop in sorted(stop_index):
            if stop == hub:
                continue
            count = min_buses(stop_routes, route_stops, hub, stop)
            if count is not None and count >= 2:
                queries.append((hub, stop, count))
                break

    return queries


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit", type=int, default=400)
    parser.add_argument("--max-appearances", type=int, default=39000)
    parser.add_argument("--routes", type=Path, default=Path("/tmp/routes.csv"))
    parser.add_argument("--stops", type=Path, default=Path("/tmp/stops.geojson"))
    parser.add_argument("--out", type=Path, default=repo / "samples" / "bmtc.txt")
    parser.add_argument(
        "--geo", type=Path, default=repo / "web" / "data" / "bmtc.json"
    )
    args = parser.parse_args()

    routes_path = download(ROUTES_URL, args.routes)
    stops_path = download(STOPS_URL, args.stops)

    coordinates = load_stop_coordinates(stops_path)
    candidates, trips_by_name = route_candidates(routes_path, coordinates)
    selected = select_routes(
        candidates, trips_by_name, args.limit, args.max_appearances
    )

    # Stable, collision-free tokens for stops and routes.
    stop_token: dict[str, str] = {}
    used_tokens: dict[str, str] = {}
    for route in selected:
        for stop in route["stops"]:
            if stop in stop_token:
                continue
            token = tokenize(stop)
            if token in used_tokens and used_tokens[token] != stop:
                suffix = 2
                while f"{token}_{suffix}" in used_tokens:
                    suffix += 1
                token = f"{token}_{suffix}"
            used_tokens[token] = stop
            stop_token[stop] = token

    route_token: dict[str, str] = {}
    used_routes: set[str] = set()
    for route in selected:
        token = tokenize(route["name"])
        if token in used_routes:
            suffix = 2
            while f"{token}_{suffix}" in used_routes:
                suffix += 1
            token = f"{token}_{suffix}"
        used_routes.add(token)
        route_token[route["name"]] = token

    for route in selected:
        route["token"] = route_token[route["name"]]
        route["stop_tokens"] = [stop_token[stop] for stop in route["stops"]]

    stop_routes, stop_degree = build_index(
        [{"name": route["token"], "stops": route["stop_tokens"]} for route in selected]
    )
    queries = choose_queries(
        [{"name": route["token"], "stops": route["stop_tokens"]} for route in selected],
        stop_routes,
    )

    # Sample input.
    lines = [
        f"# Real {CITY} bus services, converted from the unofficial BMTC GTFS.",
        f"# Source: {SOURCE} ({LICENSE}); {len(selected)} busiest route numbers.",
        "# Stop names are single tokens (spaces became _); the UI shows pretty names.",
        "# Generated by tools/import_bmtc.py - do not hand-edit.",
        f"routes {len(selected)}",
    ]
    for route in selected:
        lines.append(
            f"{route['token']} {len(route['stop_tokens'])} "
            + " ".join(route["stop_tokens"])
        )
    if queries:
        lines.append("")
        lines.append("# Demo queries used by the browser sample list.")
        for source, destination, _ in queries:
            lines.append(f"query {source} {destination}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")

    # Coordinates + pretty names for the UI. Only stops used by the sample.
    used_stops = sorted(stop_token.values())
    geo = {
        "city": CITY,
        "source": SOURCE,
        "license": LICENSE,
        "generated": "2026-11-26",
        "center": CENTER,
        "routes": len(selected),
        "stops": {},
        "display": {},
        "hubs": [],
    }
    for token in used_stops:
        original = used_tokens[token]
        lon, lat = coordinates[normalized(original)]
        geo["stops"][token] = [round(lon, 5), round(lat, 5)]
        geo["display"][token] = original
    top_hubs = sorted(
        stop_degree.items(), key=lambda item: (-item[1], item[0])
    )[:12]
    geo["hubs"] = [{"stop": token, "routes": degree} for token, degree in top_hubs]

    args.geo.parent.mkdir(parents=True, exist_ok=True)
    args.geo.write_text(
        json.dumps(geo, ensure_ascii=False, separators=(",", ":"))
    )

    total_appearances = sum(len(route["stop_tokens"]) for route in selected)
    print(f"routes:        {len(selected)}")
    print(f"appearances:   {total_appearances}")
    print(f"unique stops:  {len(used_stops)}")
    print(f"sample:        {args.out} ({args.out.stat().st_size / 1024:.0f} KB)")
    print(f"geo:           {args.geo} ({args.geo.stat().st_size / 1024:.0f} KB)")
    for source, destination, count in queries:
        print(f"query:         {source} -> {destination} = {count} buses")


if __name__ == "__main__":
    main()
