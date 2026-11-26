# Demo script

Goal: in one uninterrupted run, show a working tool, a non-obvious optimal
answer, a closure, and a performance number — then let all three members speak.

## Before you start

- [ ] Fresh clone, `make` already run (never build live).
- [ ] `make serve` already running in a browser tab, network pre-loaded.
- [ ] Terminal font large; output visible to the evaluators.
- [ ] `samples/expected1.txt` and `samples/expected-demo.txt` open in a second
      window as fallback if the live run misbehaves.
- [ ] `make test` and `bash tools/smoke.sh` are green in this terminal.
- [ ] All three members know who speaks in which act.
- [ ] No uncommitted changes: `git status` is clean.

## Act 1 — The tool exists (Role 2)

```bash
./build/journey < samples/example1.txt
```

Expected:

```
JOURNEY 2 buses
  1: R1 1 -> 7
  2: R2 7 -> 6

NO JOURNEY

JOURNEY 0 buses
  already at 7
```

Say: "The network is read once; after that every line is answered from the same
index. The second query closes R2, so the only path disappears. The third query
is the zero-bus case."

## Act 2 — Minimum buses is not minimum stops (Role 1)

```bash
./build/journey < samples/demo.txt
```

Expected:

```
JOURNEY 2 buses
  1: Outer A -> M
  2: Outer2 M -> F

JOURNEY 3 buses
  1: Hop1 A -> B
  2: Hop2 B -> C
  3: Hop3 C -> F

JOURNEY 1 buses
  1: Hop1 A -> B
```

Say: "A → F has two tempting answers. The short-looking one is three buses:
Hop1, Hop2, Hop3, four stops. The correct answer is two buses: Outer then
Outer2, ten distinct stops. A minimum-**stops** search would pick the three-bus
path. We minimise boardings, so BFS layers are buses, not stops."

Then point at the second query: "With Outer closed, the two-bus answer is gone
and the planner falls back to the three-bus journey — same index, no rebuild.
With Hop3 also closed it would print `NO JOURNEY`."

## Act 2b — Let the evaluator choose (Role 2)

```bash
./build/journey --interactive samples/city.txt
From: Jayanagar
To:   Whitefield
Closed routes (space separated, blank = none): V1
```

Say: "This is an extension on top of the batch deliverable: the same index and
the same engine, but the passenger types the origin, destination and closures.
`quit` leaves." Use it whenever an evaluator asks to pick the journey
themselves.

## Act 2c — The browser demo (two minutes, any role)

```bash
make serve      # http://127.0.0.1:8080
```

The tab opens on `samples/city.txt` and already shows Jayanagar → Whitefield.

1. Point at the result: **2 buses**, 365 then V1, change at Majestic.
2. Point at **Why this is minimal**: the layer histogram and the sentence
   "the destination first appears in layer 2". Say: "Every stop reachable with
   one bus is in layer 1. Whitefield is not there, so no one-bus journey exists."
3. Click **BFS layers**. The source moves to the centre with one dashed ring per
   bus; Whitefield sits on ring 2. Hover a stop: the tooltip reports its layer.
4. Switch back, click `V1` in the route list. The card becomes **No journey**,
   the reachable island is shown, and the result notes that one closed route was
   filtered without rebuilding the index.
5. Reopen `V1` and click **Run sweep**. It closes each route in turn and
   re-queries: `V1` is critical, the other six change nothing. Say: "401
   searches, one index."
6. Open the network chip → **Buses ≠ stops**. A → F is 2 buses over 10 stops;
   the tempting three-bus answer touches 4 stops. This is the viva question
   answered on screen.
7. Optional: paste the evaluator's network into the dialog, pick two stops by
   clicking the map, and close a route they name.

Fallback if the browser misbehaves: everything the UI shows is also available
from `./build/journey --interactive samples/city.txt`; never debug live.

## Act 3 — It scales (Role 3)

```bash
./build/journey < samples/city-large.txt | sed -n '1,12p'
time ./build/journey < samples/city-large.txt > /dev/null
```

Say: "This is a committed city dataset: 400 routes, 34,903 stop appearances,
2,001 numeric stop IDs and 100 queries, including random closures. The first two
answers are corner-to-corner journeys; the third reaches the satellite stop
9999, which only B400 serves; the fourth closes B400 and correctly reports no
journey. The whole run takes roughly 40 ms."

`make gen` still produces an even denser 40,000-appearance stress input
(`samples/big.txt`) if an evaluator asks for the contract maximum. The point to
say out loud: the index is built once, and each route is expanded at most once
per query, so the run stays proportional to the input instead of the product of
routes and stops.

## Act 4 — The tests are the spec (Role 3)

```bash
make test
```

Say: "Fifteen cases: the worked example, the closure, zero buses, disconnected
networks, backwards riding, a route that repeats a stop, repeated queries on one
index, the instrumentation agreeing with the search, and the worst-case network.
Each success is checked for chaining, active routes, both stops served, and the
known minimum."

Then the byte-exact CLI check:

```bash
bash tools/smoke.sh
```

Say: "This diffs the real binary's stdout against both checked-in expected files,
so a formatting regression cannot slip through."

## Act 5 — Why it is correct (Role 1)

One paragraph, no notes:

> "Every edge from a stop to a route costs one boarding, and every edge from a
> route to its stops costs zero, because you stay on the same bus. BFS on that
> bipartite graph therefore explores in order of boardings. The first time the
> destination is discovered, the number of boardings is minimal. Expanding each
> route once is safe because the first stop that reaches it is the closest one."

## Act 6 — Close (all three)

Each member gives their contribution sentence from
`docs/COURSE-MAPPING.md` → "Individual contribution". Then one sentence on what
you would add next (alternative journeys, custom hash table, CLI flags).

## Likely questions

| Question | Answer |
|---|---|
| Why BFS, not DFS? | DFS can find *a* path with more buses; BFS's first arrival is the minimum. |
| Why not Dijkstra? | Every bus costs 1, so BFS is Dijkstra's special case without the heap. |
| R2 lists 6 before 7 — why is 7 → 6 legal? | Routes are sets of stops; the statement gives no direction or timetable, and its own example requires this ride. |
| How do closures avoid a rebuild? | The index is untouched; inactive names are skipped while expanding routes, per query. |
| What is the complexity? | Build O(A), query O(A), memory O(A); A ≤ 40,000. |
| Two optimal journeys? | Any is valid; we assert the count and validity, not a specific path. |
| Why no `std::queue`? | Course rule: the core structures are hand-written; ours is a circular array queue. |
| What did each of you do? | The three contribution sentences; then open the owner's file and point at the function. |
| Why build a UI? | The graded tool is the CLI; the UI exists to make minimality and closures visible in one screen. It calls the same `find_journey_stats()` — no second algorithm. |
| Is the UI a dependency? | No: `make` builds both, `make test` never touches it, and the batch binary still reads stdin with no arguments. |

## Failure drill

If the binary or input misbehaves: switch to the expected-output files, explain
the design from `docs/DECISIONS.md`, and show `make test` from a fresh clone.
Never debug live in front of evaluators.
