# Handoff — current state

Last updated: **2026-11-26, 12:35 IST**

The browser UI workstream is functionally complete, committed, and pushed to
`origin/main`. This page is the short version for whoever picks it up next;
the detailed map is `docs/UI-ROLES.md`, the user-facing tour is `docs/UI.md`.

## Git state

- Branch: `main`
- **All work is committed and pushed.** Current HEAD: `5edc520`

Commit history (newest first):

| SHA | Message |
|---|---|
| `5edc520` | feat(graph): deterministic lexicographical tie-breaking; docs: add decision 20 |
| `d0594c0` | ui: add browser front-end, BFS layer view and closure sweep |
| `cc67512` | perf(graph): add hand-written StopIdMap hash table; docs: update DECISIONS.md |
| `84e9634` | feat(cli): show total stops, intermediate stops, and transfer points in interactive mode |
| `9a5fb54` | cli: add --interactive From/To/closed prompt mode |
| `27e53ad` | perf(graph): compact stop IDs in BFS and deduplicate route index |

## Verified on this machine

Run from the repo root, 2026-11-26 (Windows x86_64, MinGW g++ -O2):

```bash
make            # no warnings (-Wall -Wextra -Wpedantic)
make test       # 15 test functions, perf 19 ms (limit 1000 ms)
bash tools/smoke.sh   # 4/4 sample files byte-exact (city, city-large, demo, example1)
```

> **Note:** `src/server.cpp` requires POSIX sockets (`<arpa/inet.h>`).
> `make` on MinGW/Windows will fail for the full `journey` binary.
> Build `build/test_graph` with `mingw32-make build/test_graph`, and run
> the batch binary by pre-building on Linux/Mac or in WSL.

## What changed

| File | Change |
|---|---|
| `include/stats.hpp` | new: `QueryStats`, `find_journey_stats` |
| `src/graph.cpp` | one shared BFS (`search`) with optional stats pointer; `find_journey` is a wrapper; lexicographical tie-breaking via layer-ranked BFS + in-place merge sort; hand-written `StopIdMap` (FNV-1a open-addressing hash table); compact `IntQueue` of integer stop IDs |
| `include/server.hpp` | new: `ServerOptions`, `run_server` |
| `src/server.cpp` | new: POSIX HTTP + JSON API, static files, build/journey/impact handlers |
| `src/main.cpp` | new `--serve [--port N] [--web DIR]`; `--interactive` prompt mode; help text |
| `src/cli.cpp` | `--interactive` stop-by-stop route trace with intermediate stops, total stop count, and transfer points |
| `Makefile` | links `src/server.cpp`, `serve` and `ui-test` targets |
| `tests/test_graph.cpp` | case 15: stats agree with the search (layers, skipped closures, zero-bus); case 8 fix: use `demo_network()` |
| `tools/ui_smoke.sh` | new: end-to-end HTTP smoke check |
| `web/index.html`, `web/app.js`, `web/style.css`, `web/favicon.svg` | new: the UI |
| `README.md` | UI section, screenshot, quick start, repo map, status |
| `docs/UI.md` | new: UI tour, API, layer view, demo path; interactive CLI detail row |
| `docs/UI-ROLES.md` | new: ownership, architecture, recipes, backlog |
| `docs/HANDOFF.md` | updated: git state reflects all committed work |
| `docs/CONTRACT.md` | documents `--serve` and `find_journey_stats` as additive |
| `docs/DECISIONS.md` | rows 17–20: compact IDs, StopIdMap, interactive details, lex tie-breaking; perf log filled |
| `docs/DEMO-SCRIPT.md` | Act 2b updated with interactive stop-by-stop expected output; Act 2c browser demo |
| `docs/ROLES.md`, `docs/CONTRIBUTING.md`, `AGENTS.md` | fourth workstream + shortcut policy |
| `.github/pull_request_template.md` | UI role, `make ui-test`, docs checklist |
| `docs/wayfinder*.png` | screenshots (~900 KB total) |

The batch path is untouched behaviourally:

```bash
./build/journey < samples/example1.txt | diff - samples/expected1.txt && echo OK
```

## If you have ten minutes

1. `make && make test && make ui-test && bash tools/smoke.sh`
2. `make serve`, open <http://127.0.0.1:8080>, run the seven steps in
   `docs/DEMO-SCRIPT.md` → Act 2c.
3. Pick an unclaimed row from `docs/TASKS.md` (the specification is in
   `docs/UI-ROLES.md`; U3, U5 and U9 are small).

## Known gaps

Nothing blocks the demo. These are the honest weak spots:

- **Uncommitted.** Until it is committed, a fresh clone has no UI.
- **Single-threaded server.** One request at a time, loopback only. Fine for a
  demo; not a hosted service.
- **Browser automation is manual.** `make ui-test` checks the API, not pixels.
  Task U6 in `docs/UI-ROLES.md` covers adding Playwright for the UI part.
- **Accessibility pass implemented (U7).** Added `:focus-visible` outlines,
  form `<label>`s, `aria-labelledby` on `#networkDialog`, landmark labels,
  live regions, tab arrow navigation, `closed` text badges, dashed destination
  rings, and WCAG AA `--dim` contrast (automated `axe` run in headless
  browser still recommended once Chromium/Playwright is wired under U6).
- **Touch gestures & 390 px layout added (U8).** Two-finger pinch-zoom,
  10 px touch tap-slop pan guard, and larger mobile tap targets are implemented
  in `web/app.js` and `web/style.css` (not yet exercised on physical touch hardware).
- **Pasted networks are not shareable.** The URL hash stores the sample file
  name; a pasted network lives in memory only (U9).
- **Screenshots add ~900 KB** to the repo. Delete them if size matters more
  than the README.

## Environment

- Build: `g++ 16.2.1`, C++17, `make`, no external libraries.
- UI: vanilla ES2017 + SVG; no build step; no CDN; works offline.
- Server: binds `127.0.0.1`; default port 8080; `--web` defaults to `web`.
- Samples must be `.txt` under `samples/`; they are served read-only.
- The development server used for the screenshots was on port 8137 (not 8080)
  so it could coexist with `make serve`; it is stopped before handoff.

## Where the requirements live

| Requirement | Where to prove it |
|---|---|
| minimum buses, not stops | `src/graph.cpp` BFS comments; `make test` case 7; UI `docs/demo.txt` |
| source == destination = 0 buses | `make test` case 3; UI zero-bus card |
| ordered routes + transfer stops | `JOURNEY` output in `src/cli.cpp`; UI leg timeline |
| inactive routes per query, no rebuild | `route_expanded` + inactive set in `src/graph.cpp`; UI closure toggles |
| repeated queries on one index | `run_cli` builds once; UI `/api/journey` reuses `AppState` |
| overlap handled once | `route_expanded`; closure sweep reuses the index |
| limits 400 / 40,000 | parser and `tests/test_graph.cpp` case 14 |
| unseen network | paste dialog; `--web`/`/samples/` |
