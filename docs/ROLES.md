# Roles

Three equal roles. Pick one, claim it with the **Role claim** issue template,
and read `docs/CONTRACT.md` before writing code.

| Role | Mission | Owns |
|---|---|---|
| **1 — Core algorithm** | minimum-bus search | `src/graph.cpp` |
| **2 — CLI & I/O** | make it usable and exact | `src/cli.cpp`, `src/main.cpp` |
| **3 — Tests, perf & demo** | prove it and sell it | `tests/`, `tools/`, `docs/DEMO-SCRIPT.md` |

Shared duties for everyone:

- review the other two's PRs when you can — helpful, not a gate;
- keep `docs/DECISIONS.md` current when you make a choice;
- be able to explain your own file line by line, and the *idea* of the other two;
- rehearse the demo until you can run it without reading a script.

There is also a **fourth, additive workstream: the browser UI**. It is not part
of the graded batch contract, and it is split by surface so all three members
contribute without colliding. Read `docs/UI-ROLES.md` for ownership, extension
recipes and the task backlog; `docs/HANDOFF.md` has the current state and the
exact verification commands.

> Shortcut policy: only the **DSA core** must be hand-written (`src/graph.cpp`
> — the queue and the BFS). Tooling around it (HTTP server, JSON, UI, browser
> automation) may use pragmatic shortcuts, but `make` must stay dependency-free
> and the batch CLI output must stay byte-exact.

---

## Role 1 — Core algorithm (`src/graph.cpp`)

You implement the part the problem is actually about: the minimum-bus search.
You do **not** touch parsing, printing or test files.

### Deliverables

- [ ] `build_network` owns the stop index (already drafted — verify and keep it)
- [ ] `find_journey` returns an optimal journey, or `false`
- [ ] `source == destination` returns a valid 0-leg journey
- [ ] inactive routes are skipped, without rebuilding anything
- [ ] each route is expanded at most once per query
- [ ] your **own** queue for the BFS frontier (circular array or linked queue)
- [ ] a short comment stating the complexity, which you can defend

### Plan

1. Read `include/journey.hpp` and the "What counts as a journey" section of
   `docs/CONTRACT.md`.
2. Keep `build_network`: it maps every stop to the route indices that serve it.
   `A` stop appearances → O(A).
3. Implement the search. The shape that works:

```
find_journey(net, src, dst, inactive):
  if src == dst: return 0-leg journey

  dist[src] = 0; Q.push(src)          // Q is YOUR queue
  while Q not empty:
    s = Q.pop()
    for r in net.stop_to_routes[s]:
      if inactive(r) or expanded[r]: continue
      expanded[r] = true              // per query, sized to routes.size()
      for t in net.routes[r].stops:
        if dist has t: continue
        dist[t] = dist[s] + 1         // +1 bus, the whole point
        parent_stop[t] = s
        parent_route[t] = r
        if t == dst: rebuild and return true
        Q.push(t)
  return false

rebuild(dst):
  walk parent_stop back to src, pushing
  {routes[parent_route[cur]].name, parent_stop[cur], cur}
  at the FRONT of legs
```

4. Reset `dist`, `parent_*` and `expanded` per query. Never reset the index.
5. Turn the red tests green, then add nothing to `tests/` yourself — ask Role 3
   for new cases.

### Why this is optimal (the viva answer)

BFS explores by number of boardings. Every stop first discovered at distance
`d` is reachable with exactly `d` buses and cannot be reachable with fewer
(otherwise it would have been found in an earlier layer). Therefore the first
time `dst` is discovered, `d` is the minimum number of buses.

### Pitfalls

- **Do not count stops.** A 10-stop ride on one bus is 1 bus.
- `expanded[r]` must be per query; reusing it across queries silently loses
  journeys after the first query.
- Mark a stop visited when you **push** it, not when you pop it, or the queue
  blows up on dense networks.
- Inactive names are strings; use a set built once per query (≤400 route
  names, so even a linear scan is fine, but say why in a comment).
- A route may contain a stop twice; the `dist` check handles it.
- If `src` is unknown, `stop_to_routes` has no entry: return `false`, don't
  crash.
- Do not use `std::queue`, `std::deque`, or a library graph algorithm.

### Questions you must be able to answer

1. Why BFS and not DFS?
2. Why not Dijkstra? (every bus costs 1 — weights are uniform)
3. What exactly does `expanded[r]` prevent, and what does it cost to remove it?
4. Why is `dist[s] + 1` the right layer, even when a route serves 100 stops?
5. Why can the answer ignore the listed stop order?
6. What is the complexity of build and of one query?
7. What happens with two equally short journeys, and why is that acceptable?

### Stretch (only after `make test` is green)

- lexicographically smallest route sequence for ties;
- return alternative journeys (k-shortest by buses);
- replace `unordered_map` with a hand-written hash table if the evaluator
  wants Module-5 hashing to be visible.

---

## Role 2 — CLI & I/O (`src/cli.cpp`, `src/main.cpp`)

You make the algorithm usable: parse the network once, answer every query, and
print exactly what the contract says. You do **not** write the search or the
tests.

### Deliverables

- [ ] parse `routes <n>` and exactly `n` route lines, validating counts
- [ ] call `build_network` **once**, outside the query loop
- [ ] loop over `query` lines, call `find_journey`, print the result
- [ ] all three output forms, byte-exact, one blank line between blocks
- [ ] ignore blank lines and `#` comments
- [ ] malformed query → message on stderr, continue with the next line
- [ ] malformed network → message on stderr, return exit code 1
- [ ] `NO JOURNEY` still exits 0
- [ ] `./build/journey < samples/example1.txt` matches `samples/expected1.txt`

### Plan

1. Use `std::getline` + `std::istringstream` per line. Do not mix `>>` on the
   stream with `getline` — that is the classic beginner bug.
2. Read the header. Validate that the first token is `routes` and the count is
   a number in range.
3. Read `n` route lines into `std::vector<Route>`; check `k` matches the number
   of stops actually present and that the name is not a duplicate.
4. `Network net = build_network(routes);` — once.
5. For each remaining line: skip blank/`#`; split; expect `query`; need at
   least source and destination; the rest are inactive names.
6. Print:
   ```
   JOURNEY <legs.size()> buses
     <i>: <route> <from> -> <to>
   ```
   or `JOURNEY 0 buses` + `  already at <dst>` or `NO JOURNEY`.
   Track whether you have printed before, so the blank line goes *between*
   blocks and not after the last one.
7. Run the sample and `diff` it. Byte-exact means no trailing spaces.

### Pitfalls

- Building the index inside the query loop is the one mistake this project
  exists to catch. Build once.
- `k` can be 0; handle it.
- Don't print the warning for an unknown inactive route to stdout.
- Don't `exit(1)` on `NO JOURNEY`.
- Guard every vector index on malformed input; the demo may include a typo.
- Flush after each query if you want the live demo to print progressively.

### Questions you must be able to answer

1. Where in your code is the index built, and why is that outside the loop?
2. How does the inactive list travel from the input line to the algorithm?
3. What is the difference between a network error and a query error?
4. Why are diagnostics on stderr?
5. How did you verify the output format? (show the diff)

### Stretch

- `--pretty` flag that adds a human-readable summary on stderr;
- read from a file path argument if given, stdin otherwise;
- a query counter and total timing line on stderr for the demo.

---

## Role 3 — Tests, performance & demo (`tests/`, `tools/`)

You define "done" and prove it. You also own the story the evaluators see.

### Deliverables

- [ ] the test matrix below, all passing
- [ ] a `journey_is_valid(...)` helper that checks the contract independently
      of the implementation (chain, route serves both stops, route active)
- [ ] `tools/gen_network.cpp`: 400 routes, 40,000 appearances, heavy overlap
- [ ] a perf check with `std::chrono` and a generous threshold
- [ ] CLI smoke check: sample input → `diff` against `samples/expected1.txt`
- [ ] `docs/DEMO-SCRIPT.md` kept accurate, with expected output
- [ ] rehearse the demo with Roles 1 and 2 at least twice

### Test matrix

| # | Case | Expected |
|---|---|---|
| 1 | Problem example `1 -> 6` | 2 buses: R1 then R2, transfer at 7 |
| 2 | Same, `R2` inactive | `NO JOURNEY` |
| 3 | `7 -> 7` | 0 buses |
| 4 | Two disconnected components | `NO JOURNEY` |
| 5 | One route, forward | 1 bus |
| 6 | One route, backward (against listed order) | 1 bus |
| 7 | Dense overlapping routes | optimal count, not one more |
| 8 | Inactive route that is not needed | unchanged answer |
| 9 | Unknown source or destination | `NO JOURNEY` |
| 10 | Route visiting a stop twice | no fake loop, valid answer |
| 11 | Chain of three different buses | 3 buses, in order |
| 12 | 0-stop route in the network | ignored, no crash |
| 13 | Repeated queries on one `Network` | identical answers (index not mutated) |
| 14 | 400 routes / 40,000 appearances | under the threshold, no timeouts |

For every success case also assert:

- `legs.size() == k` reported by the journey;
- legs chain (`legs[i].to == legs[i+1].from`);
- each leg's route is active and serves both stops;
- the count is the known minimum (use cases with a unique optimum, or compare
  against the expected number, never a specific path unless it is unique).

### Perf plan

1. Generator writes to stdout in the CLI format: a pool of ~500 stops, 400
   routes of ~100 appearances each (40,000 total), deliberately overlapping,
   plus ~100 queries.
2. Time the whole run with `std::chrono::steady_clock`.
3. Fail the test above 1 second (loose on purpose — it catches accidental
   O(routes × stops) rescans, not machine speed).
4. Report the measured number, not just pass/fail; put it in `DECISIONS.md`.

### Questions you must be able to answer

1. Which case proves the answer is *minimum*, not merely valid?
2. How would a minimum-*stops* implementation fail your matrix?
3. How is the perf number measured, and what would make it explode?
4. What does the demo show, in order, and why that order?
5. Which test would catch a stale `expanded[]` flag across queries?

### Stretch

- exhaustive oracle for tiny networks (≤5 routes): enumerate every route
  sequence up to length n and compare the minimum with `find_journey`;
- random network fuzzing with the oracle;
- a `make accept` target that diffs all sample inputs against expected files.
