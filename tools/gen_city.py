#!/usr/bin/env python3
"""Generate samples/city-large.txt: a realistic, seeded large-city network.

Why this exists
---------------
samples/big.txt is the *worst case*, not a city: every route is a straight
100-stop block. The browser demo needs a network that behaves like a bus
system, so this generator models one:

  * stops sit on a 50 x 60 grid of streets, numbered row by row;
  * nine interchanges absorb most routes, so transfers happen where a real
    passenger would expect them;
  * radial corridors and a ring give routes somewhere to overlap;
  * six route classes (trunk, mainline, cross-town, local, feeder, circular)
    with their own length ranges, so no two routes are twins;
  * one village is reachable only through a single feeder, and 9999 is an
    airport served only by B400 - the demo's "close one bus, strand a place";
  * queries follow a trip model (same corridor, cross-city errand, deep
    suburb) instead of two uniformly random stops.

Everything is seeded and the committed output is reproducible byte-for-byte:

    python3 tools/gen_city.py > samples/city-large.txt
    ./build/journey < samples/city-large.txt > samples/expected-city-large.txt

The output obeys docs/CONTRACT.md: 400 routes, <= 40,000 appearances.

Python standard library only, like tools/import_bmtc.py.
"""
import random
import sys

SEED = 20261126

# Street grid. Stop id = row * WIDTH + col + 1, so the browser's sorted grid
# layout also reads as the real map (row by row).
WIDTH = 50
HEIGHT = 60
GRID_STOPS = WIDTH * HEIGHT

ROUTE_COUNT = 400
AIRPORT = 9999
VILLAGE_FIRST = GRID_STOPS + 1
VILLAGE_SIZE = 40

# Interchanges, in grid coordinates (row, col). One central railhead, eight
# outer town centres. Real networks are not uniform: everything funnels here.
CENTRE = (30, 25)
OUTER_HUBS = [
    (8, 25),    # north
    (10, 42),   # north-east
    (28, 48),   # east
    (48, 42),   # south-east
    (51, 20),   # south
    (47, 8),    # south-west
    (28, 2),    # west
    (10, 8),    # north-west
]
OPPOSITE = {0: 4, 1: 5, 2: 6, 3: 7, 4: 0, 5: 1, 6: 2, 7: 3}


def stop_id(cell):
    return cell[0] * WIDTH + cell[1] + 1


def in_grid(cell):
    return 0 <= cell[0] < HEIGHT and 0 <= cell[1] < WIDTH


def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


NEIGHBOURS = ((1, 0), (-1, 0), (0, 1), (0, -1))


def simple_path(start, waypoints, target_cells, rng, detour_p):
    """Grow a self-avoiding walk (a randomised depth-first search) that heads
    for each waypoint in turn. When a branch dead-ends the walk backs up and
    takes another one, which is why a passenger never sees the same stop
    twice and why routes have believable bends instead of straight lines."""
    path = [start]
    on_path = {start}
    waypoint = 0
    goal = waypoints[0] if waypoints else start
    guard = 0
    while (len(path) < target_cells or waypoint < len(waypoints)) \
            and guard < target_cells * 500:
        guard += 1
        if waypoint < len(waypoints) and path[-1] == waypoints[waypoint]:
            waypoint += 1
            if waypoint < len(waypoints):
                goal = waypoints[waypoint]
        current = path[-1]
        options = [nxt for dr, dc in NEIGHBOURS
                   for nxt in [(current[0] + dr, current[1] + dc)]
                   if in_grid(nxt) and nxt not in on_path]
        if not options:
            if len(path) == 1:
                break
            on_path.discard(path.pop())
            continue
        if rng.random() < detour_p:
            nxt = rng.choice(options)
        else:
            best = min(manhattan(nxt, goal) for nxt in options)
            nxt = rng.choice([nxt for nxt in options
                              if manhattan(nxt, goal) == best])
        path.append(nxt)
        on_path.add(nxt)
    return path


def sample_stops(cells, count):
    """Take `count` stops spread over the walked cells. Consecutive stops end
    up one to three blocks apart, so the route reads like a service pattern,
    not a list of every intersection."""
    picked = []
    last = len(cells) - 1
    for i in range(count):
        cell = cells[round(i * last / max(1, count - 1))]
        if not picked or picked[-1] != cell:
            picked.append(cell)
    return picked


def route_path(start, anchors, count, rng, detour_p, circular_home=None):
    """Walk enough distinct cells to place `count` stops, then sample them.
    A circular service gets its start appended as the final stop, which the
    contract explicitly allows. The walk occasionally boxes itself in and
    comes up short, so try a few independent walks before accepting that."""
    best = []
    for _ in range(4):
        target_cells = int(count * rng.uniform(1.15, 1.9)) + 4
        cells = simple_path(start, anchors, target_cells, rng, detour_p)
        if len(cells) > len(best):
            best = cells
        if len(cells) >= count:
            break
    stops = sample_stops(best, min(count, max(2, len(best))))
    if circular_home is not None:
        stops.append(circular_home)
    return stops


def nearest_hub(cell):
    return min(range(len(OUTER_HUBS)),
               key=lambda i: manhattan(cell, OUTER_HUBS[i]))


def build_routes(rng):
    """Return [(name, [cells...])] in a plausible service mix."""
    routes = []

    def add(name, cells):
        routes.append((name, cells))

    # Trunk services: one side of the city to the other through the centre.
    # Eight of them carry the radial demand, exactly as in a real network.
    for i in range(8):
        hub = OUTER_HUBS[i]
        opposite = OUTER_HUBS[OPPOSITE[i]]
        count = rng.randint(120, 160)
        cells = route_path(hub, [CENTRE, opposite], count, rng, 0.16)
        add(f"B{len(routes) + 1:03d}", cells)

    # Mainline: outer town centre to the railhead, half of them via the ring
    # road instead of the centre, so the centre does not own every route.
    for _ in range(72):
        hub_index = rng.randrange(len(OUTER_HUBS))
        hub = OUTER_HUBS[hub_index]
        count = rng.randint(90, 130)
        if rng.random() < 0.5:
            ring = OUTER_HUBS[(hub_index + rng.choice((-1, 1))) % len(OUTER_HUBS)]
            cells = route_path(hub, [ring, CENTRE], count, rng, 0.15)
        else:
            cells = route_path(hub, [CENTRE], count, rng, 0.15)
        add(f"B{len(routes) + 1:03d}", cells)

    # Cross-town: two town centres joined without going through the middle.
    # A random mid-route waypoint keeps every service from being identical.
    for _ in range(140):
        a = rng.randrange(len(OUTER_HUBS))
        b = rng.randrange(len(OUTER_HUBS))
        while b == a:
            b = rng.randrange(len(OUTER_HUBS))
        via = (rng.randrange(HEIGHT), rng.randrange(WIDTH))
        count = rng.randint(70, 110)
        cells = route_path(OUTER_HUBS[a], [via, OUTER_HUBS[b]], count, rng,
                           0.18)
        add(f"B{len(routes) + 1:03d}", cells)

    # Local: an estate circuit that still ends at the nearest town centre.
    for _ in range(90):
        home = (rng.randrange(HEIGHT), rng.randrange(WIDTH))
        via = (min(HEIGHT - 1, max(0, home[0] + rng.randint(-6, 6))),
               min(WIDTH - 1, max(0, home[1] + rng.randint(-6, 6))))
        hub = OUTER_HUBS[nearest_hub(home)]
        count = rng.randint(50, 80)
        cells = route_path(home, [via, hub], count, rng, 0.22)
        add(f"B{len(routes) + 1:03d}", cells)

    # Feeder: a short, sparse service from the edge of a district into its
    # town centre. These are the routes whose closure strands a road.
    for _ in range(60):
        hub_index = rng.randrange(len(OUTER_HUBS))
        hub = OUTER_HUBS[hub_index]
        home = (min(HEIGHT - 1, max(0, hub[0] + rng.randint(-12, 12))),
                min(WIDTH - 1, max(0, hub[1] + rng.randint(-12, 12))))
        count = rng.randint(22, 40)
        cells = route_path(home, [hub], count, rng, 0.12)
        add(f"B{len(routes) + 1:03d}", cells)

    # Circular: out of a district and back to where it started. The repeated
    # first stop is legal and common on real circular services.
    for _ in range(27):
        home = (rng.randrange(HEIGHT), rng.randrange(WIDTH))
        waypoints = []
        for _ in range(3):
            waypoints.append(
                (min(HEIGHT - 1, max(0, home[0] + rng.randint(-9, 9))),
                 min(WIDTH - 1, max(0, home[1] + rng.randint(-9, 9))))
            )
        count = rng.randint(12, 24)
        back = (min(HEIGHT - 1, max(0, home[0] + rng.choice((-1, 1)))),
                min(WIDTH - 1, max(0, home[1] + rng.choice((-1, 1)))))
        cells = route_path(home, waypoints + [back], count, rng, 0.2,
                           circular_home=home)
        add(f"B{len(routes) + 1:03d}", cells)

    # The village: forty stops off the grid, joined to the city by exactly
    # one feeder. Closing B398 makes the whole village unreachable.
    village_hub = OUTER_HUBS[2]
    village_cells = [VILLAGE_FIRST + i for i in range(VILLAGE_SIZE)]
    connector = village_cells[:12] + [stop_id(village_hub)]
    add("B398", connector)
    # A circular inside the village, starting and ending at the stop the
    # connector serves. Closing B398 then strands the whole village - not
    # just the half that the loop does not reach.
    loop = [village_cells[11]] + village_cells[12:] + [village_cells[11]]
    add("B399", loop)

    # The airport: only B400 serves 9999, so the demo can strand it.
    airport_cell = (6, 44)
    add("B400", [AIRPORT, stop_id(airport_cell), stop_id(OUTER_HUBS[1])])

    return routes


def fill_coverage(routes, rng):
    """Attach stops that no route happened to serve. A stop within two blocks
    of a route is inserted next to its nearest served neighbour; anything
    further out is left unserved (a corner with no service does not exist)."""
    served = set()
    for _, cells in routes:
        served.update(cells)
    missing = [(r, c) for r in range(HEIGHT) for c in range(WIDTH)
               if (r, c) not in served]
    if not missing:
        return routes, 0

    # Spatial index: every grid stop points at nearby route positions. This
    # keeps the join linear instead of scanning all 31k appearances 3k times.
    neighbourhood = {}
    for route_index, (_, cells) in enumerate(routes):
        for position, cell in enumerate(cells):
            if not isinstance(cell, tuple):
                continue  # village stops are not on the street grid
            for dr in range(-2, 3):
                for dc in range(-2, 3):
                    if abs(dr) + abs(dc) > 2:
                        continue
                    key = (cell[0] + dr, cell[1] + dc)
                    neighbourhood.setdefault(key, []).append(
                        (route_index, position))

    inserts = {}
    for cell in missing:
        if rng.random() > 0.4:
            continue  # not every street corner warrants a stop
        candidates = neighbourhood.get(cell)
        if not candidates:
            continue
        route_index, position = rng.choice(candidates)
        inserts.setdefault(route_index, []).append((position, cell))

    added = 0
    for route_index, positions in inserts.items():
        cells = routes[route_index][1]
        for position, cell in sorted(positions, reverse=True):
            cells.insert(position + 1, cell)
            added += 1
    return routes, added


def build_queries(routes, rng):
    """A trip model, not two dice: most people ride one corridor or cross the
    city once; a few are local errands or deep-suburb trips."""
    stop_routes = {}
    for name, cells in routes:
        for cell in cells:
            stop_routes.setdefault(cell, []).append(name)
    served = [cell for cell in stop_routes if isinstance(cell, tuple)]

    grid_routes = [(name, cells) for name, cells in routes
                   if cells and isinstance(cells[0], tuple)]

    def route_pair():
        name, cells = rng.choice(grid_routes)
        if len(cells) < 2:
            return None
        i = rng.randrange(len(cells) - 1)
        j = min(len(cells) - 1, i + rng.randint(1, 30))
        if i == j:
            return None
        return cells[i], cells[j]

    def near(cell, radius):
        r = min(HEIGHT - 1, max(0, cell[0] + rng.randint(-radius, radius)))
        c = min(WIDTH - 1, max(0, cell[1] + rng.randint(-radius, radius)))
        return (r, c)

    def serving(cell):
        return list(stop_routes.get(cell, []))

    queries = []

    def add(src, dst, closed):
        queries.append((src, dst, list(dict.fromkeys(closed))))

    def served_near(cell, radius):
        """A destination that actually exists: snap a random offset to the
        nearest served stop, so an errand does not become an unknown stop."""
        guess = near(cell, radius)
        return min(served, key=lambda stop: manhattan(stop, guess))

    airport_stop = AIRPORT
    airport_hub = stop_id((6, 44))
    village_stop = VILLAGE_FIRST + rng.randrange(VILLAGE_SIZE)
    village_connector = "B398"

    # Airports and villages are the demonstration cases: they exist to show
    # that one closed route can disconnect a place completely.
    add(airport_stop, airport_stop, [])
    add(airport_stop, airport_hub, [])
    add(airport_stop, airport_hub, ["B400"])
    add(village_stop, stop_id(OUTER_HUBS[2]), [])
    add(village_stop, stop_id(OUTER_HUBS[2]), [village_connector])
    add(village_stop, village_stop, [])

    while len(queries) < 97:
        kind = rng.random()
        pair = None
        if kind < 0.38:
            # Same corridor: the everyday one-bus ride.
            pair = route_pair()
        elif kind < 0.62:
            # Cross-city: two different town centres.
            a = rng.randrange(len(OUTER_HUBS))
            b = rng.randrange(len(OUTER_HUBS))
            while b == a:
                b = rng.randrange(len(OUTER_HUBS))
            pair = (served_near(OUTER_HUBS[a], 5),
                    served_near(OUTER_HUBS[b], 5))
        elif kind < 0.82:
            # Local errand: a few streets away.
            home = rng.choice(served)
            pair = (home, served_near(home, 7))
        else:
            # Deep suburb: both ends are leaves, so the trip needs a feeder
            # and at least one change.
            leaves = [cell for cell, names in stop_routes.items()
                      if isinstance(cell, tuple) and len(names) <= 2]
            if leaves:
                pair = (rng.choice(leaves), rng.choice(leaves))
        if pair is None or pair[0] == pair[1]:
            continue
        src, dst = pair
        closed = []
        roll = rng.random()
        if roll < 0.55:
            closed = []
        elif roll < 0.86:
            pool = serving(src) + serving(dst)
            if pool:
                closed = [rng.choice(pool)]
        elif roll < 0.96:
            pool = serving(src) + serving(dst)
            if len(pool) >= 2:
                closed = rng.sample(pool, 2)
        else:
            pool = serving(src) + serving(dst)
            if len(pool) >= 3:
                closed = rng.sample(pool, 3)
        if closed and rng.random() < 0.04:
            # A typo in an ops notice is a warning, not an error.
            closed[0] = "B999"
        add(stop_id(src), stop_id(dst), closed)

    # Three more everyday closures: a commuter's direct service is off, and
    # the query is really "what now?".
    for start, end in ((CENTRE, OUTER_HUBS[4]), (OUTER_HUBS[5], OUTER_HUBS[1]),
                       (OUTER_HUBS[2], OUTER_HUBS[6])):
        names = serving(start)
        if names:
            add(stop_id(start), stop_id(end), [names[0]])

    return queries


def write_network(routes, queries, appearances):
    out = sys.stdout.write
    stops = {cell for _, cells in routes for cell in cells}
    out("# Fictional city generated by tools/gen_city.py; do not edit by hand.\n")
    out(f"# Seed {SEED}; {ROUTE_COUNT} routes, {appearances:,} stop "
        f"appearances across\n")
    out(f"# {len(stops):,} stops: a {WIDTH}x{HEIGHT} street grid, a "
        f"{VILLAGE_SIZE}-stop village and\n")
    out("# the airport 9999. Nine interchanges; B400 alone serves the airport and\n")
    out("# B398 alone serves the village. Query mix: corridors, cross-city,\n")
    out("# local errands, deep-suburb trips and a few closed routes.\n")
    out(f"routes {ROUTE_COUNT}\n")
    for name, cells in routes:
        tokens = [str(AIRPORT if cell == AIRPORT else
                      (cell if isinstance(cell, int) else stop_id(cell)))
                  for cell in cells]
        out(f"{name} {len(tokens)} " + " ".join(tokens) + "\n")
    out("\n")
    for src, dst, closed in queries:
        line = f"query {src} {dst}"
        if closed:
            line += " " + " ".join(closed)
        out(line + "\n")


def main():
    rng = random.Random(SEED)
    routes = build_routes(rng)
    routes, added = fill_coverage(routes, rng)

    # Keep the contract's 40,000-appearance ceiling with headroom; a route
    # longer than the class range would also be a generation bug.
    appearances = sum(len(cells) for _, cells in routes)
    if appearances > 40000:
        raise SystemExit(f"generated {appearances} appearances (> 40,000)")
    if len(routes) > ROUTE_COUNT:
        raise SystemExit(f"generated {len(routes)} routes (> {ROUTE_COUNT})")

    queries = build_queries(routes, rng)
    write_network(routes, queries, appearances)
    print(f"# coverage helper inserted {added} stops", file=sys.stderr)


if __name__ == "__main__":
    main()
