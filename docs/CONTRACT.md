# Contract — frozen at kickoff

Everything here was agreed before coding started. It is the single source of
truth for all three roles. Changing it needs a PR that the other two have seen,
and any change to `include/journey.hpp` must be in that same PR.

## 1. The program

`build/journey` reads a network and a list of queries from **stdin** and writes
results to **stdout**. No file arguments, no flags, no network access.

## 2. Input grammar

One item per line, tokens separated by whitespace.

```
routes <n>
<route-name> <k> <stop1> <stop2> ... <stopk>     (exactly n lines)
query <source> <destination> [<inactive-route> ...]   (zero or more lines)
```

Rules:

| Rule | Detail |
|---|---|
| Header | The first non-blank, non-comment line must be `routes <n>`. It appears exactly once. |
| Comments | A line whose first non-space character is `#` is ignored. No inline comments. |
| Blank lines | Ignored anywhere. |
| Names | Route and stop names are single tokens, case-sensitive. |
| Limits | `0 <= n <= 400`, `k >= 0`, total stop appearances `<= 40,000`. |
| Route names | Unique within a network. A duplicate is a **network error**. |
| Repeated stops | A route may list a stop twice; it is treated as one served stop. |
| Queries | Any number, in any order after the network. Each may list zero or more inactive route names. |
| Unknown names | An unknown **inactive** route name is a warning, not an error. An unknown **stop** simply produces `NO JOURNEY`. |

There is deliberately no "rebuild" command: the index is built once.

### Example (`samples/example1.txt`)

```
routes 2
R1 3 1 2 7
R2 3 3 6 7

query 1 6
query 1 6 R2
query 7 7
```

## 3. Output

For **each** query, print exactly one block to stdout. Separate consecutive
blocks with one blank line. Never print anything else to stdout.

Success, `k > 0`:

```
JOURNEY <k> buses
  1: <route> <from> -> <to>
  2: <route> <from> -> <to>
```

Success, `k == 0`:

```
JOURNEY 0 buses
  already at <destination>
```

No journey:

```
NO JOURNEY
```

Details:

- Always the word `buses`, even for 1 — one less special case to test.
- `<k>` is the number of legs, which is the number of times a bus is boarded.
- Legs are in travel order. The transfer stop after leg *i* is leg *i*'s `to`
  and leg *i+1*'s `from`, so it is visible without a separate line.
- Leg *i*'s `from` and `to` must both be served by `route`, but they do not
  have to be adjacent in the route's stop list.
- Diagnostics go to **stderr** as `ERROR: ...` or `WARNING: ...`.
- Exit code: `0` if the network was read successfully (even if every answer is
  `NO JOURNEY`), `1` if the network itself is malformed.

Expected output for the example above: `samples/expected1.txt`.

## 4. What counts as a journey (tests enforce this)

A `Journey` is **valid** for `(source, destination, inactive)` when:

1. `legs[0].from == source` and `legs.back().to == destination`.
2. Consecutive legs chain: `legs[i].to == legs[i+1].from`.
3. Every leg's route is **not** inactive and serves both `from` and `to`.
4. `legs.empty()` is allowed only when `source == destination`.

It is **optimal** when no valid journey exists with fewer legs. The reported
`<k>` must equal `legs.size()`.

- Direction is ignored: a route is a set of stops, not a timetable. The
  problem's own example requires boarding `R2` at 7 and alighting at 6 even
  though `R2 = [3, 6, 7]`.
- If several optimal journeys exist, **any** of them is correct. Tests assert
  validity and leg count, not one specific path.
- `source == destination` is a successful **0-bus** journey, even if the stop is
  not in the network.
- Inactive routes are unusable for that query; they stay in the index and are
  filtered during the search. Never rebuild the index for a query.

## 5. API contracts

```cpp
Network build_network(const std::vector<Route>& routes);
```
- Builds all query-independent data. Called **once** per network.
- Must not read stdin, must not depend on any query.
- Complexity: O(A), where A is total stop appearances.

```cpp
bool find_journey(const Network& network,
                  const std::string& source,
                  const std::string& destination,
                  const std::vector<std::string>& inactive_routes,
                  Journey& out);
```
- `true` + filled `out` on success; `false` when no journey exists.
- `const`: must not modify `network`; safe to call repeatedly with different
  inactive lists.
- Must not rebuild the index and must not read input.
- Complexity target: O(A) worst case per query. A route must be expanded at
  most once per query, so heavily overlapping services are not rescanned.

`int run_cli(std::istream&, std::ostream&)` owns parsing and printing and
returns the process exit code.

## 6. Performance target

With 400 routes and 40,000 stop appearances:

- build: a single pass over the appearances;
- query: well under 100 ms on a laptop, and the loose acceptance test in
  `make test` must stay under 1 s for the whole generated batch.

## 7. Non-goals

No timetables/frequencies, no walking transfers, no fares, no distances, no
travel time, no preference ranking, no persistence. Every bus costs exactly 1.

## 8. Ownership

| Path | Owner |
|---|---|
| `include/journey.hpp` | all three (tell the others before changing) |
| `src/graph.cpp` | Role 1 |
| `src/cli.cpp`, `src/main.cpp` | Role 2 |
| `tests/`, `tools/` | Role 3 |
| `samples/` | Role 3 (add cases, do not change `example1`) |
