# Wayfinder — complete project brief for an LLM

> **How to use this file:** upload it and say:
> "You are helping a student defend this project in a viva/demo. Answer only
> from this document, in short spoken answers, and cite file:line when useful.
> If something is not covered, say you don't know instead of inventing it."
>
> The student has ~20 minutes. Prioritise the DSA core, the contract, the tests,
> the real-data caveat, and the demo script.

---

## 1. The project in one paragraph

**Problem Statement 10 — "The City That Couldn't Find Its Way"** (Graph
Traversal and Route Planning, Data Structures and its Applications, 1BCS305,
DSCE).

A city has overlapping bus routes. Given routes as ordered lists of stops, a
source, a destination, and optionally some closed routes, return a valid journey
using the **minimum number of buses boarded** — not the minimum number of stops.
Show the ordered buses and the transfer stops. `source == destination` is a
0-bus journey. Repeated queries must reuse one index. Limits: 400 routes,
40,000 stop appearances.

Deliverable: a byte-exact batch CLI (`./build/journey`), a hand-written BFS, a
test matrix, and an optional browser UI that calls the same search.

**One-sentence answer to "what did you build?"**
> "A minimum-bus journey planner: a hand-written BFS over a bipartite
> stop↔route graph, with an index built once, per-query route closures, and a
> browser UI that visualises the BFS layers."

---

## 2. Current state (2026-11-26)

- Branch `main`; the remote tip includes the BMTC data, the redesigned UI,
  the 15-case CLI contract suite and the 17-function graph suite. The DSA core
  (`src/graph.cpp`) is unchanged from the commit with the lexicographic
  tie-break.
- Relevant commits: `0cab40c` real BMTC data, `a792a9e` data/licence panel +
  refresh check, `4591382` light theme + opt-in `--threads`/`--log`, `c7dc6e2`
  tie-break test + 500-network oracle fuzzer, `ba1b542` CLI contract suite.
- `samples/city-large.txt` is produced by `tools/gen_city.py`: 400 routes /
  29,975 appearances / 2,953 stops (seed 20261126).
- Verified: `make` clean; `make test` **17 test functions**, perf **~27 ms** for
  100 queries on 400 routes / 40,000 appearances; `make cli-test` 15 contract
  cases; `bash tools/smoke.sh` **5/5** byte-exact; `make ui-test` OK;
  `make real-check` OK.
- A dev server may already run at <http://127.0.0.1:8080>; otherwise
  `make serve` (default port 8080).

---

## 3. Repository map

```
include/journey.hpp      frozen contract: Route, Network, Leg, Journey, API
include/stats.hpp        additive QueryStats + find_journey_stats (UI only)
include/server.hpp       ServerOptions + run_server
src/graph.cpp            THE DSA: index, queue, hash table, sort, BFS
src/cli.cpp              parser + exact output + interactive mode
src/main.cpp             entry point: batch / --interactive / --serve
src/server.cpp           POSIX HTTP + JSON, calls find_journey_stats
tests/test_graph.cpp     15 test functions, perf check
tests/check.hpp          tiny assertion macros
tools/gen_network.cpp    400-route / 40,000-appearance stress generator
tools/gen_city.py        seeded realistic city generator (city-large)
tools/import_bmtc.py     real BMTC GTFS → contract sample
tools/check_bmtc.py      validates sample, coordinates, queries, leg chains
tools/render_bmtc_svg.py standalone BMTC map generator
tools/refresh_bmtc.py    provenance/manifest check for the BMTC sample
tools/cli_test.sh        15-case CLI contract suite (`make cli-test`)
tools/smoke.sh           byte-exact sample diffs (5 samples)
tools/ui_smoke.sh        HTTP API smoke (build/journey/closure/sweep/static)
tools/ui_demo_check.py   browser demo check (U6)
web/                     vanilla JS/SVG single-page UI, no framework/CDN
samples/                 example1, demo, city, city-large, bmtc + expected
docs/                    CONTRACT, DECISIONS, ROLES, UI, REAL-DATA, TASKS,
                         HANDOFF, DEMO-SCRIPT, COURSE-MAPPING
```

---

## 4. The core DSA — `src/graph.cpp` (392 lines)

### 4.1 Data structures (`include/journey.hpp:22-48`)

```cpp
struct Route  { string name; vector<string> stops; };
struct Network {
  vector<Route> routes;
  unordered_map<string, vector<int>> stop_to_routes;  // inverted index
};
struct Leg    { string route, from, to; };
struct Journey{ vector<Leg> legs; };                  // size() == buses
```

`Network.stop_to_routes` is the adjacency list of a **bipartite stop↔route
graph**: for a stop, the indices of all routes that serve it.

### 4.2 The mental model

```
stop --(cost 1 bus)--> route --(cost 0)--> stop
```

BFS is over **boardings**, not stops:

- layer 0 = source
- layer 1 = every stop reachable with exactly 1 bus
- layer 2 = every stop reachable with exactly 2 buses
- …

A 50-stop ride is still one layer. The first layer containing the destination is
the minimum bus count.

### 4.3 Hand-written structures (course requirement)

| Structure | Location | Details |
|---|---|---|
| Circular queue | `src/graph.cpp:17-42` (`IntQueue`) | `vector<int>` ring, `head_`, `tail_`, `size_`; capacity = number of distinct stops (or 1 if zero); each stop is enqueued at most once per query, so it cannot overflow |
| FNV-1a hash | `src/graph.cpp:45-52` (`hash_stop`) | 64-bit offset basis `14695981039346656037`, prime `1099511628211` |
| Open-addressing hash table | `src/graph.cpp:57-112` (`StopIdMap`) | linear probing, power-of-two capacity, rehash at 50% load, stores non-owning `const string*` keys into `network.routes`/`source`, plus cached hash and compact int ID |
| In-place merge sort | `src/graph.cpp:115-144` (`sort_layer`) | used only for the lexicographic tie-break; avoids `<algorithm>` |

Why hand-written: the course requires the core structures to be visible; they
are Module 2 (queue), Module 5 (hashing), and sorting. The graded path must not
use `std::queue`, `std::deque`, or a library BFS.

### 4.4 `build_network` — `src/graph.cpp:151-164`

One pass over every stop appearance:

```text
for each route r:
  for each stop s in r.stops:
    list = stop_to_routes[s]
    if list is empty or list.back() != r:   // dedupe consecutive duplicates
      list.push_back(r)
```

- Called **once** per network (`src/cli.cpp:183`, and once in the server).
- Complexity **O(A)**, A = total stop appearances (≤ 40,000).
- A route that lists a stop twice is still one served stop; non-consecutive
  duplicates can add the route index twice, which is harmless.

### 4.5 `search` — `src/graph.cpp:185-373`

This single function is the DSA. `find_journey` calls it with `nullptr` stats;
`find_journey_stats` calls it with a stats pointer for the UI. Same traversal.

Step by step:

1. **Zero-bus** (`:195`): if `source == destination`, return true with empty
   legs. Valid even if the stop is not in the network.
2. **Unknown endpoint** (`:203`): if either stop is absent from
   `stop_to_routes`, return false.
3. **Inactive set** (`:209-215`): a small `unordered_map<string,char>` built per
   query from `inactive_routes`. Never touches the index.
4. **Per-query arrays** (`:221-228`):
   - `StopIdMap visited` — name → compact ID, plus `insert_or_get`
   - `id_to_name` — compact ID → pointer to the stop string
   - `stop_layer` — buses needed to reach the stop
   - `stop_rank` — lexicographic prefix rank within a layer
   - `parent_stop`, `parent_route` — for reconstruction
   - `route_expanded` — one `char` per route, **local to this query**
5. **Initialise** (`:230-242`): source gets ID 0, layer 0, rank 0, parent -1;
   push 0 into the queue.
6. **Layer loop** (`:250-320`): pop exactly `layer_count` stops — the whole
   current boarding layer. For each stop:
   - look up its routes in `stop_to_routes`;
   - skip a route already expanded **in this query**;
   - mark it expanded;
   - skip it if its name is in the inactive set;
   - for every stop on the route, `insert_or_get`:
     - if new: record layer `current+1`, parents, push to `next_layer`;
       remember destination ID;
     - if already seen **at the same layer**: maybe update its parent if the new
       prefix is lexicographically smaller.
7. **Reconstruct** (`:322-338`): after the layer finishes, if the destination
   was found, walk `parent_stop`/`parent_route` back to the source, build legs,
   reverse. Legs are in travel order.
8. **Next layer** (`:340-369`): if `next_layer` is empty, the active component
   is exhausted → false. Otherwise sort `next_layer` by `(parent rank, parent
   route name)`, assign ranks, push into the circular queue, increment the
   layer.

### 4.6 Why the answer is minimal (the viva proof)

> "Every stop→route edge costs one bus and every route→stop edge costs zero, so
> BFS explores in nondecreasing numbers of boardings. A stop first discovered at
> layer d is reachable with exactly d buses. If the destination were reachable
> with fewer, BFS would have discovered it in an earlier layer. Therefore the
> first layer containing the destination is the minimum."

### 4.7 Why expanding each route once is safe

The first stop that reaches a route is the closest one (BFS order); expanding the
route again from a later stop could only produce journeys with at least as many
buses. `route_expanded[r]` caps the work at one expansion per route per query,
which keeps a query at **O(A)** on overlapping networks.

### 4.8 Complexity

- Build: **O(A)** time, **O(A + S + R)** memory.
- Query (core BFS): **O(A)**.
- Query with lexicographic tie-break: **O(A + S log S)** worst case (per-layer
  merge sort); still tiny at the limits.
- Measured: **26–27 ms for 100 queries** on 400 routes / 40,000 appearances
  (limit 1,000 ms), i7-13620H, g++ 16.2.1.

### 4.9 Lexicographic tie-breaking (`:253-369`)

Added after the core was correct; it changes **which** optimal journey is
printed, never the bus count.

- `stop_rank` stores a prefix rank within the layer.
- The layer is processed in rank order.
- When an already-seen stop is reachable at the same layer, its parent is
  replaced if the new parent has a smaller rank, or equal rank and a smaller
  route name.
- The next layer is sorted by `(parent rank, parent route name)` before being
  queued, then ranks are assigned.
- `sort_layer` is the hand-written merge sort.

If asked "why is there a sort in a BFS?", answer: "only for deterministic
tie-breaking; the BFS correctness is from the layer/queue, not the sort."

---

## 5. The contract (`docs/CONTRACT.md`)

### 5.1 Input

```
routes <n>
<route-name> <k> <stop1> ... <stopk>     (exactly n lines)
query <source> <destination> [inactive-route ...]   (zero or more)
```

- First non-blank, non-comment line must be `routes <n>`; appears once.
- Lines whose first non-space char is `#` are comments; blank lines ignored.
- Names are single tokens, case-sensitive.
- `0 <= n <= 400`, `k >= 0`, total appearances `<= 40,000`.
- Route names unique; duplicate is a **network error**.
- A route may repeat a stop; treated as one served stop.
- Unknown inactive route → warning on stderr, not an error.
- Unknown stop → `NO JOURNEY`.
- No rebuild command: the index is built once.

### 5.2 Output (stdout only)

Success with k > 0:

```
JOURNEY <k> buses
  1: <route> <from> -> <to>
  2: <route> <from> -> <to>
```

0 buses:

```
JOURNEY 0 buses
  already at <destination>
```

No journey:

```
NO JOURNEY
```

- Always the word `buses`, even for 1.
- One blank line **between** blocks, never trailing.
- Legs in travel order; transfer is leg i's `to` and leg i+1's `from`.
- Diagnostics only on stderr (`ERROR:` / `WARNING:`).
- Exit 0 if the network parsed (even if all answers are `NO JOURNEY`);
  exit 1 if the network is malformed.

### 5.3 API semantics

```cpp
Network build_network(const vector<Route>& routes);       // once, O(A)
bool find_journey(const Network&, source, destination,
                  const vector<string>& inactive, Journey& out);
```

- `find_journey` is `const`: no mutation, safe to call repeatedly.
- Inactive routes stay in the index; filtered during search.
- Any optimal journey is valid; tests assert validity + leg count, not a
  specific path unless it is unique.

---

## 6. CLI and interactive extension

### Batch

```bash
make
./build/journey < samples/example1.txt
./build/journey samples/example1.txt    # same, input file as an argument
```

Exact output for `samples/example1.txt`:

```
JOURNEY 2 buses
  1: R1 1 -> 7
  2: R2 7 -> 6

NO JOURNEY

JOURNEY 0 buses
  already at 7
```

### Interactive

```bash
./build/journey --interactive samples/city.txt
From: Jayanagar
To:   Whitefield
Closed routes (space separated, blank = none):
```

It prints the normal block **plus** route details and a summary (added by
Role 2):

```
JOURNEY 2 buses
  1: 210 Jayanagar -> Majestic
  2: V1 Majestic -> Whitefield
Route details:
  1: 210 (3 stops): Jayanagar -> Lalbagh -> Majestic
  2: V1 (5 stops): Majestic -> MGRoad -> Domlur -> Marathahalli -> Whitefield
Summary: 2 buses, 7 total stops, 1 transfer (change at: Majestic)
```

`trace_leg_stops` (`src/cli.cpp:223-266`) chooses the shortest slice between the
two stops when a stop appears more than once on a route. Interactive prompts
never appear in batch output.

---

## 7. Tests — `tests/test_graph.cpp` (17 functions) + CLI contract suite

| Function | What it proves |
|---|---|
| `test_index_maps_stops_to_routes` | index correctness (5 stops, stop 7 on routes 0 and 1) |
| `test_same_stop_is_zero_buses` | 0-bus journey, known and unknown stop |
| `test_problem_statement_example` | R1 then R2, transfer at 7, valid + exact legs |
| `test_inactive_route_blocks_the_only_path` | closure → NO JOURNEY |
| `test_disconnected_components` | no path → NO JOURNEY |
| `test_one_route_forward_and_backward` | undirected routes (both directions) |
| `test_minimum_buses_not_minimum_stops` | 2 buses / 10 stops beats 3 buses / 4 stops |
| `test_inactive_route_not_needed` | closing an unused route changes nothing |
| `test_unknown_source_or_destination` | NO JOURNEY, no crash |
| `test_route_visiting_stop_twice` | no fake loop |
| `test_chain_of_three_buses` | 3 buses in order |
| `test_zero_stop_route` | empty route ignored |
| `test_repeated_queries_use_the_same_index` | index not mutated; identical answers |
| `test_stats_match_the_search` | UI stats agree with BFS (layers, skipped closures, 0-bus) |
| `test_worst_case_performance` | 100 queries / 400 routes / 40,000 appearances < 1 s |
| `test_lexicographical_tie_breaking` | equal-bus journeys pick the smallest route-name sequence |
| `test_random_networks_against_oracle` | 500 random networks: BFS bus count equals an independent oracle, every leg valid |

`journey_is_valid` re-derives validity from the raw routes (chain, route serves
both stops, route active), rather than trusting the search's parents.

Run: `make test` (17 graph tests). `make cli-test` runs `tools/cli_test.sh`
(15 end-to-end contract cases: grammar, exit codes, warnings, limits, exact
output). Smoke: `bash tools/smoke.sh` (5 samples, byte-exact). Real data:
`make real-check`.

---

## 8. The browser UI (optional, additive)

- Run: `make serve` → <http://127.0.0.1:8080>.
- Flags: `journey --serve [--port N] [--web DIR] [--threads N] [--log]`.
- POSIX HTTP server, loopback only, no external libraries; **default 1 thread**.
- `--threads N` (1..16) is opt-in; `--log` logs each request to stderr.
- SPA: `web/index.html`, `web/app.js`, `web/style.css`; no framework, no CDN,
  works offline.
- The browser **never reimplements BFS**. Every answer, layer count and timing
  comes from `POST /api/journey`, which calls `find_journey_stats()`.
- Endpoints:
  - `GET /api/network` — routes, stops, appearances, hubs
  - `POST /api/build` — `{name, routes:[{name,stops:[]}]}`, builds once
  - `POST /api/journey` — `{source, destination, inactive:[]}` → legs,
    transfers, layers, reached stops, timing
  - `POST /api/impact` — closes each route in turn, re-queries, reports
    critical/longer/unchanged
  - `GET /samples/<file>.txt`, `GET /data/bmtc.json`, static assets
- UI features:
  - route map with pan/zoom, click-to-set From/To, closure toggles;
  - animated journey and transfer rings;
  - **BFS layers view**: source at centre, one ring per bus, exact per-layer
    counts, hover layer;
  - "Why this is minimal" histogram;
  - **closure sweep**: one re-query per route on the same index;
  - real BMTC sample with geographic coordinates;
  - recent queries, shareable hash for samples, paste an unseen network.
- Instrumentation: `include/stats.hpp`, `QueryStats`; `find_journey` passes
  `nullptr` so the graded path pays nothing.
- Single-threaded by default; `--threads N` is opt-in for the demo, one network,
  one index.
- Reachable list capped at 2,500 stops in JSON; layer **counts** stay exact and
  the legend discloses truncation.

---

## 9. Real BMTC data (`docs/REAL-DATA.md`)

- Source: `https://github.com/Vonter/bmtc-gtfs` — unofficial BMTC GTFS scraped
  from the Namma BMTC app; **ODbL 1.0**; timetables are known to be imperfect.
- Selected: **400 busiest route numbers**, one direction per number (the longer
  pattern), **12,325 appearances**, **2,175 stops**, all with coordinates.
- Files: `samples/bmtc.txt`, `samples/expected-bmtc.txt`,
  `web/data/bmtc.json`, `tools/import_bmtc.py`, `tools/check_bmtc.py`.
- Regenerate: `make data` (needs network once; caches routes.csv and
  stops.geojson in /tmp); `make real-check` validates.
- Stop names are single tokens (`Kempegowda_Bus_Station(Majestic/KBS)`); the
  JSON keeps the pretty name for display. The CLI output uses tokens.
- Routes remain **undirected** even though GTFS has UP/DOWN patterns; one name
  per route fits the frozen grammar. This is a demo sample, not a timetable.
- Demo queries:
  - Jayanagar → Hebbal: **2 buses**, `60-A` + `KIA-10`, change at Mysore Road.
  - KBS → Airport: **1 bus**, `KIA-9`.
  - Banashankari → Marathahalli: **3 buses**, `MF-34`, `226-N`, `333-E`.
  - KBS → Electronic City: **1 bus**, `349-C`.
- Attribution: "BMTC GTFS data by Vonter/bmtc-gtfs, ODbL 1.0. Not affiliated
  with BMTC."

---

## 10. Samples and demo script

| File | What it is |
|---|---|
| `samples/example1.txt` | the problem statement's worked example |
| `samples/demo.txt` | 2 buses / 10 stops beats 3 buses / 4 stops |
| `samples/city.txt` | 7 named routes, Bengaluru-style |
| `samples/city-large.txt` | 400 routes / 29,975 appearances / 2,953 stops (seeded generator) |
| `samples/bmtc.txt` | 400 real BMTC services, 2,175 stops, coordinates |

Two-minute demo:

1. `make && make test` — 15 tests, perf number.
2. `./build/journey < samples/example1.txt` — 2 buses, closure, 0 buses.
3. `./build/journey < samples/demo.txt` — 2 buses beats the 3-bus short path.
4. `./build/journey --interactive samples/city.txt` — let the evaluator choose.
5. `make serve` → real BMTC map, layers view, close a route, run the sweep.
6. `bash tools/smoke.sh` — byte-exact 5/5.

Key sentence for step 3: "The tempting answer is three buses and four stops; the
correct answer is two buses and ten stops. We optimise boardings, not stops."

---

## 11. Likely viva questions and answers

**Q: Where is the DSA?**
`src/graph.cpp`: index `build_network:151`, BFS `search:185`, queue `:17`, hash
table `:57`, merge sort `:115`. Types in `include/journey.hpp`. Parser/output in
`src/cli.cpp` is I/O, not the DSA.

**Q: What are the graph's vertices and edges?**
Bipartite: stop nodes and route nodes; stop→route costs 1 bus, route→stop costs
0. Stops are not directly connected; a route is a hyperedge connecting its stops.

**Q: Why BFS?**
BFS explores in nondecreasing edge cost; here edge cost = one boarding, so the
first layer containing the destination is the minimum number of buses.

**Q: Why not DFS?**
DFS can find a path with more buses; its first arrival is not minimal.

**Q: Why not Dijkstra?**
Every bus costs exactly 1, so BFS is Dijkstra without a heap. Dijkstra would be
correct but heavier for no benefit.

**Q: Why not BFS on stops with unit edges?**
Because riding 50 stops on one bus is still 1 bus; a stop-level BFS would count
stops, optimising the wrong quantity.

**Q: Why expand each route only once per query?**
The first stop that reaches a route is closest; re-expanding from a later stop
could only add buses. It bounds the query by O(A) and handles heavy overlap.

**Q: Is `route_expanded` global or per query?**
Local to `search` (a `vector<char>` of `routes.size()`), reset every query.

**Q: Why build the index once?**
The spec requires repeated queries without rebuilding; 40,000 appearances. The
index maps every stop to the routes that serve it, O(A) build.

**Q: How are closures handled without rebuilding?**
`inactive` is a per-query set of names; when a route is about to be expanded, it
is skipped if its name is in the set. The index is never modified.

**Q: Why are routes undirected?**
The problem's own example boards `R2` at 7 and alights at 6 while
`R2 = [3,6,7]`; there are no timetables or directions. A route is a set.

**Q: What if source == destination?**
Return true with zero legs, even if the stop is unknown. Contract says 0-bus is
valid.

**Q: Unknown source or destination otherwise?**
Return false → `NO JOURNEY`.

**Q: Unknown inactive route?**
Warning on stderr, ignored; not an error.

**Q: Duplicate route name?**
Network error → stderr, exit 1.

**Q: A route repeats a stop?**
Legal; treated as one served stop. The visited map prevents a loop.

**Q: Zero-stop route?**
Legal in the grammar (`k = 0`); it is simply never useful.

**Q: How are legs reconstructed?**
`parent_stop` and `parent_route` are recorded when a stop is first discovered;
walk back from the destination to the source, build legs, reverse.

**Q: Why not store the path in the queue?**
That would copy a path per queue entry. Parent pointers are O(1) extra per stop
and reconstruct once at the end.

**Q: What is the queue's capacity and why?**
Number of distinct stops (or 1). Each stop is enqueued at most once per query, so
the ring cannot overflow. Head/tail wrap with `% size`.

**Q: Why a hand-written hash table?**
Course rule and performance: avoid string copies/hashing in the hot loop; compact
IDs index the parallel arrays. FNV-1a + open addressing with linear probing;
rehash at 50% load; keys are non-owning pointers into `network.routes` or
`source`, stable for the call.

**Q: What if two stops hash to the same slot?**
Linear probing: advance `idx = (idx + 1) & mask` until an empty slot; compare
cached hash then string on lookup. Load factor is kept ≤ 0.5 by rehashing.

**Q: What is the tie-break?**
When several journeys use the same minimum number of buses, pick the
lexicographically smallest sequence of route names. Implemented with per-layer
ranks and `sort_layer`; it never changes the bus count.

**Q: Does the tie-break affect complexity?**
Yes: O(S log S) worst case for the per-layer sorts, still O(A + S log S), well
under the limit.

**Q: Why finish the layer after finding the destination?**
So the tie-break can still improve the destination's parent within the same
layer. Plain BFS could return immediately; the bus count would be the same.

**Q: Why mark visited on insert, not on pop?**
Otherwise the same stop can be enqueued many times by overlapping routes, which
bloats the queue in dense networks.

**Q: What happens if you remove `route_expanded`?**
Overlapping routes get rescanned from every reached stop; dense networks become
quadratic and the perf test catches it.

**Q: What happens if you rebuild the index per query?**
You violate the repeated-query requirement and add O(A) per query; the 100-query
perf test would still pass at 40k, but the design requirement is explicit.

**Q: Complexity and memory?**
Build O(A), query O(A) (plus O(S log S) with tie-break), memory
O(A + S + R). A ≤ 40,000.

**Q: Measured performance?**
26–27 ms for 100 queries on 400 routes / 40,000 appearances; limit 1,000 ms.
`city-large` (400 / 29,975) is ~0.5–1.3 ms per query; closure sweep over 400
routes ~170 ms.

**Q: How do you know it is correct, not just valid?**
Tests assert the known minimum on crafted cases, including the min-buses vs
min-stops case; `journey_is_valid` independently checks chain, active routes and
both stops served; the worst-case perf test checks all 100 answers are valid.

**Q: What is the `city-large` data?**
A seeded generator (`tools/gen_city.py`) creating corridors, hubs, feeders and
uneven route lengths: 400 routes, 29,975 appearances, 2,953 stops, degrees
1–65, lengths 3–156. It replaced a uniform lattice so the demo looks like a city.

**Q: What is the BMTC sample and its caveat?**
400 busiest real BMTC route numbers converted from the unofficial open GTFS
(ODbL). One direction per number because the grammar has one name per route;
times/fares are not used; the data source is unofficial.

**Q: Why do stop names have underscores?**
The contract splits on whitespace, so names must be single tokens. The UI maps
tokens back to pretty names via `web/data/bmtc.json`.

**Q: Does the browser recompute BFS?**
No. It calls `/api/journey`; `src/server.cpp` calls `find_journey_stats()` in the
same `src/graph.cpp`. `find_journey` (graded) and `find_journey_stats` share one
`search` function; the graded path passes `nullptr`.

**Q: What is the closure sweep?**
`POST /api/impact` runs `find_journey` once per route with that route closed,
reporting stranded / longer / unchanged — 401 searches on one index.

**Q: What is not implemented?**
Timetables, frequencies, fares, distances, travel time, walking transfers,
alternative ranking, persistence. Scope says they are not needed.

**Q: What would you improve?**
Real GTFS route shapes on the map; k-shortest alternatives; accessibility and
touch polish; a min-stops comparison for teaching. Say the first two.

**Q: Does the server support threads?**
The default is one thread, matching the course's deterministic demo and making
the index sharing obvious. `--threads N` (1..16) is an opt-in flag for the demo;
`--log` prints each request. The search itself is read-only and per-query state
is local, so parallel requests share the index safely.

**Q: Why no `std::queue` / Boost / CMake?**
Course rule for the core; the build must stay `make`, dependency-free, and every
member must explain the structures. The UI is additive and may use pragmatic
tooling, but the graded CLI does not.

**Q: Why is the output exactly this format?**
Byte-exact tests and `diff` against expected files; grid/format regressions are
caught by `tools/smoke.sh`.

**Q: What are the limits?**
400 routes, 40,000 appearances, enforced by the parser; the server also enforces
them. `samples/city-large` and the generated stress input are at the limit.

---

## 12. Role contribution scripts

Use your own words, but these are the facts.

**Role 1 — Core algorithm (`src/graph.cpp`)**
> "I built the stop→routes index once, then the minimum-bus BFS. Each route is
> expanded at most once per query, closures are filtered per query, and parent
> pointers reconstruct the legs. I also added the lexicographic tie-break. The
> correctness argument is BFS by boardings."

**Role 2 — CLI & I/O (`src/cli.cpp`, `src/main.cpp`)**
> "I own the parser and the exact output. The parser validates the header, route
> counts, duplicates and the 40,000 limit, builds the index once, and loops over
> queries. I also added the interactive mode with route details and the
> `--serve` entry point."

**Role 3 — Tests, perf, demo (`tests/`, `tools/`)**
> "I wrote the 17 graph tests and the 15-case CLI contract suite, the independent
> validator, the 500-network oracle fuzzer, the 40,000-appearance generator, the
> byte-exact smoke test, the API smoke, and the demo script. My tests assert
> validity and the known minimum, not one specific path."

If asked "what did each of you do?", point at the ownership table in
`docs/ROLES.md` and open your file.

---

## 13. Known limitations and honest caveats

- Real BMTC GTFS is **unofficial**; timetables are imperfect; only route order
  and coordinates are used. One direction per route; no shapes (routes are drawn
  through stop coordinates).
- The UI server is single-threaded by default (opt-in `--threads N`), loopback
  only, no auth; fine for a demo.
- `samples/city-large.txt` is generated by `tools/gen_city.py`; a live session
  may have local output that differs from the committed sample.
- The tie-break changes which optimal journey is printed; expected city fixtures
  were regenerated for it.

---

## 14. Glossary

- **Boarding / bus / leg** — one entry into a route; one `Leg`.
- **Layer** — all stops reachable with the same number of boardings.
- **Inactive/closed route** — a route excluded for one query only.
- **Index** — `stop_to_routes`, built once.
- **Route pattern** — the ordered stop list of one service.
- **Tie-break** — lexicographically smallest route sequence among equal-bus
  journeys.
- **GTFS** — General Transit Feed Specification (routes.txt, stops.txt, ...).
- **ODbL** — Open Database License, the BMTC GTFS data licence.

---

## 15. If the LLM is asked to write an answer

Use this shape for "explain X":

1. What it is in one sentence.
2. Where it is (`file:line`).
3. Why it is done that way (the alternative and why rejected).
4. What it costs (complexity / trade-off).
5. How it is verified (test or command).

For "why is this the minimum?", use the BFS-layer proof, then point at
`test_minimum_buses_not_minimum_stops` and the example in `samples/demo.txt`.

For "what did you do?", answer in first person, one paragraph, and name a file
you can open. Do not claim work you cannot explain line by line.
