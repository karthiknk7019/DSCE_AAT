#!/usr/bin/env python3
"""Render the real BMTC sample as a standalone SVG map.

This is deliberately independent of the web UI: it reads the committed sample,
the coordinates, and the verified CLI output, then draws a city-wide map with
the first demo journey highlighted. It is useful for the README, for slides,
and as a fallback if the browser map misbehaves during the demo.

Usage:
  python3 tools/render_bmtc_svg.py
  python3 tools/render_bmtc_svg.py --out docs/bmtc-map.svg
"""

from __future__ import annotations

import argparse
import html
import json
import math
import re
from pathlib import Path

BG = "#070b14"
ROUTE = "#2f6273"
STOP = "#0b1424"
STOP_EDGE = "rgba(148,163,184,0.35)"
MINT = "#5eead4"
VIOLET = "#a78bfa"
AMBER = "#fbbf24"
TEXT = "#e9eefb"
MUTED = "#8494ad"
DIM = "#5d6d87"


def parse_sample(path: Path) -> tuple[list[dict], list[tuple[str, str]]]:
    lines = [
        line.strip()
        for line in path.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    header = lines[0].split()
    count = int(header[1])
    routes = []
    for line in lines[1 : 1 + count]:
        parts = line.split()
        routes.append({"name": parts[0], "stops": parts[2:]})
    queries = []
    for line in lines[1 + count :]:
        parts = line.split()
        if parts and parts[0] == "query" and len(parts) >= 3:
            queries.append((parts[1], parts[2]))
    return routes, queries


def parse_expected(path: Path) -> list[list[dict]]:
    journeys = []
    legs: list[dict] = []
    for line in path.read_text().splitlines():
        line = line.rstrip()
        if line.startswith("JOURNEY"):
            legs = []
        elif line.startswith("NO JOURNEY"):
            legs = []
            journeys.append([])
        elif re.match(r"^\s+\d+:", line):
            match = re.match(r"^\s+\d+:\s+(\S+)\s+(\S+)\s+->\s+(\S+)", line)
            if match:
                legs.append(
                    {"route": match.group(1), "from": match.group(2), "to": match.group(3)}
                )
        elif not line.strip() and legs:
            journeys.append(legs)
            legs = []
    if legs:
        journeys.append(legs)
    return journeys


def hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower = []
    for point in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0:
            lower.pop()
        lower.append(point)
    upper = []
    for point in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", type=Path, default=repo / "samples" / "bmtc.txt")
    parser.add_argument("--geo", type=Path, default=repo / "web" / "data" / "bmtc.json")
    parser.add_argument(
        "--expected", type=Path, default=repo / "samples" / "expected-bmtc.txt"
    )
    parser.add_argument("--out", type=Path, default=repo / "docs" / "bmtc-map.svg")
    args = parser.parse_args()

    routes, queries = parse_sample(args.sample)
    geo = json.loads(args.geo.read_text())
    display = geo["display"]
    stop_geo = geo["stops"]
    journeys = parse_expected(args.expected)
    journey = journeys[0] if journeys else []

    width, height = 1600, 1200
    margin_x, top, bottom = 70, 132, 70
    center_lon, center_lat = geo["center"]
    lon_scale = math.cos(math.radians(center_lat))

    raw = {
        stop: (
            (coordinate[0] - center_lon) * lon_scale,
            center_lat - coordinate[1],
        )
        for stop, coordinate in stop_geo.items()
    }
    xs = [point[0] for point in raw.values()]
    ys = [point[1] for point in raw.values()]
    span_x = max(xs) - min(xs)
    span_y = max(ys) - min(ys)
    scale = min((width - margin_x * 2) / span_x, (height - top - bottom) / span_y)
    offset_x = width / 2 - ((min(xs) + max(xs)) / 2) * scale
    offset_y = top + (height - top - bottom) / 2 - ((min(ys) + max(ys)) / 2) * scale

    def project(stop: str) -> tuple[float, float] | None:
        point = raw.get(stop)
        if point is None:
            return None
        return (offset_x + point[0] * scale, offset_y + point[1] * scale)

    out: list[str] = []
    add = out.append
    add(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" font-family="Inter, Segoe UI, sans-serif">'
    )
    add(f'<rect width="{width}" height="{height}" fill="{BG}"/>')

    # Subtle dot grid, same language as the app.
    add('<defs><pattern id="dots" width="26" height="26" patternUnits="userSpaceOnUse">')
    add('<circle cx="1" cy="1" r="1" fill="#94a3b8" opacity="0.10"/></pattern></defs>')
    add(f'<rect width="{width}" height="{height}" fill="url(#dots)" opacity="0.55"/>')

    # City hull.
    projected_stops = [point for point in (project(stop) for stop in stop_geo) if point]
    city = hull(projected_stops)
    if len(city) >= 3:
        path = " ".join(
            ("M" if index == 0 else "L") + f" {x:.1f} {y:.1f}"
            for index, (x, y) in enumerate(city)
        )
        add(
            f'<path d="{path} Z" fill="#38bdf8" fill-opacity="0.030" '
            f'stroke="#94a3b8" stroke-opacity="0.16" stroke-width="1" '
            f'stroke-dasharray="4 9"/>'
        )

    # Every route through its real stop coordinates.
    add('<g stroke-linecap="round" stroke-linejoin="round">')
    for route in routes:
        points = [project(stop) for stop in route["stops"]]
        points = [point for point in points if point]
        if len(points) < 2:
            continue
        path = " ".join(
            ("M" if index == 0 else "L") + f" {x:.1f} {y:.1f}"
            for index, (x, y) in enumerate(points)
        )
        add(
            f'<path d="{path}" fill="none" stroke="{ROUTE}" stroke-width="1.15" '
            f'stroke-opacity="0.42"/>'
        )
    add("</g>")

    # Stops.
    add(f'<g fill="{STOP}" stroke="{STOP_EDGE}" stroke-width="0.8">')
    for stop, (x, y) in zip(stop_geo.keys(), projected_stops):
        add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="1.7"/>')
    add("</g>")

    # Journey: draw each leg along the route's stop order between from and to.
    route_index = {route["name"]: route["stops"] for route in routes}
    leg_colors = [MINT, VIOLET, "#f472b6", "#fb923c"]
    for index, leg in enumerate(journey):
        stops = route_index.get(leg["route"], [])
        try:
            a = stops.index(leg["from"])
            b = stops.index(leg["to"])
        except ValueError:
            a, b = -1, -1
        if a < 0 or b < 0:
            segment = [leg["from"], leg["to"]]
        else:
            low, high = min(a, b), max(a, b)
            segment = stops[low : high + 1]
            if a > b:
                segment.reverse()
        points = [project(stop) for stop in segment]
        points = [point for point in points if point]
        if len(points) < 2:
            continue
        path = " ".join(
            ("M" if point_index == 0 else "L") + f" {x:.1f} {y:.1f}"
            for point_index, (x, y) in enumerate(points)
        )
        color = leg_colors[index % len(leg_colors)]
        add(
            f'<path d="{path}" fill="none" stroke="{color}" stroke-opacity="0.22" '
            f'stroke-width="16" stroke-linecap="round" stroke-linejoin="round"/>'
        )
        add(
            f'<path d="{path}" fill="none" stroke="{color}" stroke-width="4.2" '
            f'stroke-linecap="round" stroke-linejoin="round"/>'
        )
        # Route-number pill on the leg midpoint.
        middle = points[len(points) // 2]
        badge = leg["route"]
        badge_width = len(badge) * 6.2 + 16
        add(
            f'<rect x="{middle[0] - badge_width / 2:.1f}" y="{middle[1] - 10:.1f}" '
            f'width="{badge_width:.1f}" height="20" rx="10" fill="{BG}" '
            f'stroke="{color}" stroke-width="1.4"/>'
        )
        add(
            f'<text x="{middle[0]:.1f}" y="{middle[1] + 3.5:.1f}" text-anchor="middle" '
            f'font-size="9.5" font-weight="700" fill="{color}">{html.escape(badge)}</text>'
        )

    # Endpoint / transfer markers.
    if journey:
        source = project(journey[0]["from"])
        destination = project(journey[-1]["to"])
        for point, color in ((source, MINT), (destination, AMBER)):
            if point:
                add(
                    f'<circle cx="{point[0]:.1f}" cy="{point[1]:.1f}" r="11" '
                    f'fill="none" stroke="{color}" stroke-width="2"/>'
                )
                add(
                    f'<circle cx="{point[0]:.1f}" cy="{point[1]:.1f}" r="4" '
                    f'fill="{color}"/>'
                )
        for leg in journey[:-1]:
            point = project(leg["to"])
            if point:
                add(
                    f'<circle cx="{point[0]:.1f}" cy="{point[1]:.1f}" r="9" '
                    f'fill="{BG}" stroke="{AMBER}" stroke-width="2"/>'
                )
                add(
                    f'<circle cx="{point[0]:.1f}" cy="{point[1]:.1f}" r="3" '
                    f'fill="{AMBER}"/>'
                )

    # Labels: journey endpoints/transfers first, then hubs that do not collide.
    label_stops: list[str] = []
    if journey:
        label_stops.append(journey[0]["from"])
        label_stops.append(journey[-1]["to"])
        label_stops.extend(leg["to"] for leg in journey[:-1])
    journey_labels = set(label_stops)
    label_stops.extend(hub["stop"] for hub in geo.get("hubs", [])[:8])

    # Points on the highlighted journey, used to keep hub labels off the line.
    journey_points: list[tuple[float, float]] = []
    for leg in journey:
        stops = route_index.get(leg["route"], [])
        try:
            a = stops.index(leg["from"])
            b = stops.index(leg["to"])
        except ValueError:
            continue
        segment = stops[min(a, b) : max(a, b) + 1]
        journey_points.extend(
            point for point in (project(stop) for stop in segment) if point
        )

    def too_close_to_journey(point: tuple[float, float]) -> bool:
        return any(
            (point[0] - other[0]) ** 2 + (point[1] - other[1]) ** 2 < 34 * 34
            for other in journey_points
        )

    seen: set[str] = set()
    placed: list[tuple[float, float, float, float]] = []
    for stop in label_stops:
        if stop in seen:
            continue
        seen.add(stop)
        point = project(stop)
        if not point:
            continue
        if stop not in journey_labels and too_close_to_journey(point):
            continue
        name = display.get(stop, stop.replace("_", " "))
        name = re.sub(r"\s*\([^)]*\)", "", name)
        if len(name) > 28:
            name = name[:27] + "…"
        text_width = len(name) * 6.4 + 10
        anchor = "middle"
        dx = 0
        if point[0] < 220:
            anchor, dx = "start", 9
        elif point[0] > width - 220:
            anchor, dx = "end", -9
        left = point[0] + dx - (text_width / 2 if anchor == "middle" else 0)
        if anchor == "end":
            left = point[0] + dx - text_width
        box = (left, point[1] - 26, left + text_width, point[1] - 6)
        if any(
            box[0] < other[2]
            and box[2] > other[0]
            and box[1] < other[3]
            and box[3] > other[1]
            for other in placed
        ):
            continue
        placed.append(box)
        add(
            f'<text x="{point[0] + dx:.1f}" y="{point[1] - 12:.1f}" '
            f'text-anchor="{anchor}" font-size="12" font-weight="600" '
            f'fill="{TEXT}" stroke="{BG}" stroke-width="4" paint-order="stroke" '
            f'stroke-linejoin="round">{html.escape(name)}</text>'
        )

    # Title and legend.
    add(
        f'<text x="{margin_x}" y="64" font-size="13" letter-spacing="6" '
        f'font-weight="700" fill="{MINT}">BENGALURU</text>'
    )
    add(
        f'<text x="{margin_x}" y="93" font-size="25" font-weight="650" '
        f'fill="{TEXT}">400 BMTC services, one minimum-bus plan</text>'
    )
    add(
        f'<text x="{margin_x}" y="116" font-size="12" fill="{DIM}">'
        f'Real stop coordinates · 2,175 stops · 12,325 stop appearances · '
        f'fewest buses, not fewest stops</text>'
    )

    legend_y = height - 38
    add(
        f'<line x1="{margin_x}" y1="{legend_y}" x2="{margin_x + 46}" y2="{legend_y}" '
        f'stroke="{MINT}" stroke-width="3"/>'
    )
    add(
        f'<text x="{margin_x + 56}" y="{legend_y + 4}" font-size="11" '
        f'fill="{MUTED}">one bus leg</text>'
    )
    add(
        f'<circle cx="{margin_x + 170}" cy="{legend_y}" r="4" fill="{AMBER}"/>'
    )
    add(
        f'<text x="{margin_x + 182}" y="{legend_y + 4}" font-size="11" '
        f'fill="{MUTED}">transfer</text>'
    )
    add(
        f'<circle cx="{margin_x + 260}" cy="{legend_y}" r="3" fill="{STOP}" '
        f'stroke="{STOP_EDGE}" stroke-width="1"/>'
    )
    add(
        f'<text x="{margin_x + 272}" y="{legend_y + 4}" font-size="11" '
        f'fill="{MUTED}">stop</text>'
    )
    add(
        f'<text x="{width - margin_x}" y="{legend_y + 4}" text-anchor="end" '
        f'font-size="10" fill="{DIM}">Data: BMTC GTFS · Vonter/bmtc-gtfs · '
        f'ODbL 1.0 · unofficial</text>'
    )
    add(
        f'<text x="{width - margin_x}" y="{legend_y + 22}" text-anchor="end" '
        f'font-size="10" fill="{DIM}">Generated by tools/render_bmtc_svg.py</text>'
    )

    # Journey summary, top-right.
    if journey:
        add(
            f'<text x="{width - margin_x}" y="64" text-anchor="end" '
            f'font-size="12" fill="{MUTED}">{len(journey)} buses</text>'
        )
        readable = " → ".join(leg["route"] for leg in journey)
        add(
            f'<text x="{width - margin_x}" y="86" text-anchor="end" '
            f'font-size="15" font-weight="600" fill="{TEXT}">{html.escape(readable)}</text>'
        )
        add(
            f'<text x="{width - margin_x}" y="108" text-anchor="end" '
            f'font-size="11" fill="{DIM}">{html.escape(display.get(journey[0]["from"], ""))}'
            f' → {html.escape(display.get(journey[-1]["to"], ""))}</text>'
        )

    add("</svg>")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(out) + "\n")
    print(f"wrote {args.out} ({args.out.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
