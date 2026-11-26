#!/usr/bin/env python3
"""Validate the committed real-BMTC sample end to end.

Checks the things a teammate can break while regenerating the data:

  - samples/bmtc.txt follows the contract grammar and limits;
  - every stop has a coordinate and a display name in web/data/bmtc.json;
  - every query names stops that exist in the network;
  - samples/expected-bmtc.txt has exactly one output block per query;
  - every leg in the expected output uses a real route that serves both stops.

Exit code 0 = all checks passed, 1 = a check failed.

Usage: python3 tools/check_bmtc.py
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

MAX_ROUTES = 400
MAX_APPEARANCES = 40000


def fail(message: str) -> None:
    print(f"check-bmtc: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    sample_path = repo / "samples" / "bmtc.txt"
    expected_path = repo / "samples" / "expected-bmtc.txt"
    geo_path = repo / "web" / "data" / "bmtc.json"

    for path in (sample_path, expected_path, geo_path):
        if not path.exists():
            fail(f"missing {path.relative_to(repo)}")

    lines = [
        line.strip()
        for line in sample_path.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    header = lines[0].split()
    if len(header) != 2 or header[0] != "routes" or not header[1].isdigit():
        fail("first content line must be 'routes <n>'")
    declared = int(header[1])
    if declared > MAX_ROUTES:
        fail(f"{declared} routes exceeds the {MAX_ROUTES} limit")

    route_lines = lines[1 : 1 + declared]
    if len(route_lines) != declared:
        fail(f"declared {declared} routes, found {len(route_lines)}")

    routes: dict[str, list[str]] = {}
    appearances = 0
    for number, line in enumerate(route_lines, start=2):
        parts = line.split()
        if len(parts) < 2:
            fail(f"line {number}: expected '<route> <k> <stops...>'")
        name, count_text = parts[0], parts[1]
        if name in routes:
            fail(f"line {number}: duplicate route {name}")
        if not count_text.isdigit():
            fail(f"line {number}: stop count must be an integer")
        stops = parts[2:]
        if int(count_text) != len(stops):
            fail(
                f"line {number}: {name} declares {count_text} stops, got {len(stops)}"
            )
        if len(stops) < 2:
            fail(f"line {number}: {name} has fewer than 2 stops")
        # Repeated stops are legal (docs/CONTRACT.md): a route is a set.
        routes[name] = stops
        appearances += len(stops)

    if appearances > MAX_APPEARANCES:
        fail(f"{appearances} appearances exceeds the {MAX_APPEARANCES} limit")

    queries: list[tuple[str, str]] = []
    for line in lines[1 + declared :]:
        parts = line.split()
        if not parts:
            continue
        if parts[0] != "query" or len(parts) < 3:
            fail(f"unexpected trailing line: {line}")
        queries.append((parts[1], parts[2]))

    geo = json.loads(geo_path.read_text())
    for key in ("stops", "display", "hubs", "center", "source", "license"):
        if key not in geo:
            fail(f"bmtc.json is missing '{key}'")

    served = {stop for stops in routes.values() for stop in stops}
    missing_coords = sorted(stop for stop in served if stop not in geo["stops"])
    if missing_coords:
        fail(f"{len(missing_coords)} stops have no coordinate, e.g. {missing_coords[:3]}")
    missing_names = sorted(stop for stop in served if stop not in geo["display"])
    if missing_names:
        fail(f"{len(missing_names)} stops have no display name, e.g. {missing_names[:3]}")

    for stop, coordinate in geo["stops"].items():
        if stop not in served:
            continue
        lon, lat = coordinate
        if not (77.0 <= lon <= 78.2 and 12.4 <= lat <= 13.7):
            fail(f"{stop} has an implausible coordinate ({lon}, {lat})")

    for source, destination in queries:
        if source not in served:
            fail(f"query source {source} is not served")
        if destination not in served:
            fail(f"query destination {destination} is not served")

    expected = expected_path.read_text().splitlines()
    blocks: list[list[str]] = []
    current: list[str] = []
    for line in expected:
        if line.startswith(("JOURNEY", "NO JOURNEY")):
            if current:
                blocks.append(current)
            current = [line]
        elif line.strip() and current:
            current.append(line)
    if current:
        blocks.append(current)
    if len(blocks) != len(queries):
        fail(f"{len(queries)} queries but {len(blocks)} output blocks in expected file")

    for index, block in enumerate(blocks):
        if block[0].startswith("NO JOURNEY"):
            continue
        match = re.match(r"^JOURNEY (\d+) buses$", block[0])
        if match is None:
            fail(f"block {index + 1}: bad JOURNEY header: {block[0]}")
        buses = int(match.group(1))
        legs = block[1:]
        if buses != len(legs):
            fail(f"block {index + 1}: header says {buses}, {len(legs)} legs printed")
        previous = queries[index][0]
        for leg in legs:
            leg_match = re.match(r"^\s+\d+:\s+(\S+)\s+(\S+)\s+->\s+(\S+)$", leg)
            if leg_match is None:
                fail(f"block {index + 1}: bad leg line: {leg}")
            route, source, destination = leg_match.groups()
            if route not in routes:
                fail(f"block {index + 1}: unknown route {route}")
            if source not in routes[route] or destination not in routes[route]:
                fail(f"block {index + 1}: {route} does not serve {source} -> {destination}")
            if source != previous:
                fail(f"block {index + 1}: leg does not chain at {source}")
            previous = destination
        if previous != queries[index][1]:
            fail(f"block {index + 1}: journey does not end at {queries[index][1]}")

    print(
        f"check-bmtc: OK - {len(routes)} routes, {appearances} appearances, "
        f"{len(served)} stops, {len(queries)} queries, "
        f"{len(blocks)} output blocks"
    )


if __name__ == "__main__":
    main()
