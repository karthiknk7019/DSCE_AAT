# Demo script

Goal: in one uninterrupted run, show a working tool, a non-obvious optimal
answer, a closure, and a performance number — then let all three members speak.

## Before you start

- [ ] Fresh clone, `make` already run (never build live).
- [ ] Terminal font large; output visible to the evaluators.
- [ ] `samples/expected1.txt` and `samples/expected-demo.txt` open in a second
      window as fallback if the live run misbehaves.
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
Hop1, Hop2, Hop3, four stops. The correct answer is two buses: Outer across
seven stops, then Outer2. A minimum-**stops** search would pick the three-bus
path. We minimise boardings, so BFS layers are buses, not stops."

Then point at the second query: "With Outer closed, the two-bus answer is gone
and the planner falls back to the three-bus journey — same index, no rebuild.
With Hop3 also closed it would print `NO JOURNEY`."

## Act 3 — It scales (Role 3)

```bash
make gen
time ./build/journey < samples/big.txt
```

Say: "400 routes, 40,000 stop appearances, heavy overlap, a hundred queries.
The index is built once. Each route is expanded at most once per query, so the
run stays proportional to the input instead of the product of routes and stops."

Report the measured wall time out loud.

## Act 4 — The tests are the spec (Role 3)

```bash
make test
```

Say: "Fourteen cases: the worked example, the closure, zero buses, disconnected
networks, backwards riding, a route that repeats a stop, repeated queries on one
index, and the worst-case network. Each success is checked for chaining, active
routes, both stops served, and the known minimum."

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

## Failure drill

If the binary or input misbehaves: switch to the expected-output files, explain
the design from `docs/DECISIONS.md`, and show `make test` from a fresh clone.
Never debug live in front of evaluators.
