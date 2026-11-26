# AGENTS.md — rules for AI assistants in this repo

Read this before changing anything. This is a graded, 3-person student project
for a Data Structures course. Correctness and explainability beat cleverness.

## Hard rules

1. **C++17 only.** No external libraries, no Boost, no package managers.
   Allowed from the standard library: `vector`, `string`, `unordered_map`,
   `istringstream`, `iostream`, `chrono` (tests only).
2. **Do not use `std::queue`, `std::deque` or any library BFS/graph algorithm
   for the search frontier.** The team hand-writes the queue (circular array or
   linked queue). This is a course requirement, not a style preference.
3. **Do not edit files outside the task you were given.** Ownership:
   `src/graph.cpp` → Role 1, `src/cli.cpp` + `src/main.cpp` → Role 2,
   `tests/` + `tools/` → Role 3. `include/journey.hpp` is frozen and needs all
   three owners to agree.
4. **Do not rewrite whole files.** Make targeted edits that preserve the
   surrounding code and comments. Large rewrites cause merge conflicts between
   three people working in parallel.
5. **No new dependencies, no build-system changes** (no CMake, no Conan).
   The build is `make`.
6. **Do not weaken tests to make them pass.** If a test fails, fix the code or
   explain why the test is wrong — in the PR, not silently.
7. **No secrets.** Never commit tokens, credentials or `.env` files.
8. **Claim your work in `docs/TASKS.md` before you change code.** The board is
   the single source of truth for who is doing what. Set your row to `claimed`
   with your name/session, and set it to `done` with the verifying command and
   commit when it passes. If you are not assigned a task, take the first
   `unclaimed` P0 row, then P1 in ID order — do not wait to be asked. If you
   find unlisted work, add a row first. Never edit a row you do not own except
   to unclaim it or add a note.

## Before you finish any task

```bash
make            # must build with no warnings
make test       # must pass, except the documented red tests
make run        # must match samples/expected1.txt
```

If you changed behaviour, update `docs/DECISIONS.md`.
If you changed the interface, stop and ask: `include/journey.hpp` is frozen.
Mark your `docs/TASKS.md` row `done` with the command you ran.

## Style

- C++17, 2-space indent, ~80 columns.
- Types `PascalCase`, functions and variables `snake_case`, constants
  `kPascalCase`.
- No `using namespace std;` in headers. Prefer explicit `std::`.
- Comment *why*, not *what*. Keep functions small enough to explain in a viva.
- Every non-obvious algorithm step gets a one-line comment the author can
  defend out loud.

## UI / tooling exception

The hand-written and restricted-header rules above protect the **DSA core**:
the queue and the BFS in `src/graph.cpp`. The optional browser workstream
(`src/server.cpp`, `web/`, the API test) may use pragmatic shortcuts and
third-party tooling, because it is not the graded algorithm and it ships no
runtime dependency:

- `make` must still build with no external libraries and no network;
- `./build/journey` with no arguments must still match `samples/expected1.txt`
  byte-for-byte;
- the browser must never reimplement BFS — it calls the same
  `find_journey_stats()`;
- `include/journey.hpp` stays frozen.

See `docs/UI-ROLES.md` for the split and `docs/CONTRIBUTING.md` §5 for the
updated policy.

## Domain facts that are easy to get wrong

- We minimise **buses boarded**, not stops visited. One BFS layer = one bus.
- Routes are **undirected**: a leg may run against the listed stop order. The
  problem's own example boards `R2` at 7 and alights at 6 while
  `R2 = [3, 6, 7]`.
- The index is built **once**; `find_journey` must not rebuild it and must not
  read input.
- Inactive routes are filtered **per query**; the index stays untouched.
- `source == destination` is a valid **0-bus** journey.
- Output format is exact; see `docs/CONTRACT.md`.
