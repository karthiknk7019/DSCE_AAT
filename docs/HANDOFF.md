# Handoff — current state

Last updated: **2026-11-26, 13:25 IST**

`main` now has the browser UI, the teammate features U4–U12/R8, and the real
BMTC sample. The planner redesign and the real geo map are pushed on
`ui/bmtc-redesign` but **not merged**: they conflict with the teammate UI work.
This page is the short version for whoever picks it up next; the detailed map is
`docs/UI-ROLES.md`, the user-facing tour is `docs/UI.md`.

## Git state

- Branch: `main`; HEAD `0cab40c` (real BMTC data) plus this docs commit.
- Review branch: `ui/bmtc-redesign` — 4 commits (`fccfc86`, `78ba616`,
  `8e3a0a0`, `9424cb6`): real-data docs, the planner redesign + geo map, the
  Act 2c browser check, and the seeded city generator.
- The redesign must be rebased over `a35e02c` and merged by hand: `web/app.js`,
  `web/index.html` and `web/style.css` conflict with U4–U12/R8. Do not drop the
  teammate features. Task U16 in `docs/TASKS.md` tracks it.

Recent commits on `main` (newest first):

| SHA | Message |
|---|---|
| `0cab40c` | data: add real BMTC sample, importer, coverage check and geo map |
| `a35e02c` | tools: add Act 2c browser demo check (U6) |
| `5c68ffa` | ui: add dense-map keyboard stop search and focus overlay (R8) |
| `a36bd61` | ui: export map as SVG and PNG (U12); docs: mark U12 done |
| `564c49b` | ui: persist pasted network in localStorage (U9); docs: mark U5 & U9 done |
| `7bf4d56` | ui: add accessibility focus rings, labels, ARIA, and non-colour cues (U7) |
| `d3fe57c` | ui: add touch pinch-zoom, tap-slop guard, and 390px tap targets (U8) |
| `0d47e1c` | ui: add route path hover tooltip (U5) |

## Verified on this machine

Run from the repo root, 2026-11-26 (CachyOS, g++ 16.2.1):

```bash
make                  # no warnings (-Wall -Wextra -Wpedantic)
make test             # 15 test functions, perf 28 ms (limit 1000 ms)
make ui-test          # API build/journey/closure/sweep/static all OK
bash tools/smoke.sh   # 5/5 sample files byte-exact, including real BMTC
python3 tools/check_bmtc.py  # OK: 400 routes, 12,325 appearances, 2,175 stops
bash tools/ui_demo_check.sh  # Act 2c steps 1-6 green on ui/bmtc-redesign
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
| `samples/bmtc.txt` | new: 400 real BMTC services, 12,325 appearances, 2,175 stops |
| `samples/expected-bmtc.txt` | new: byte-exact CLI output, included in `tools/smoke.sh` |
| `web/data/bmtc.json` | new: real stop coordinates + pretty names + attribution |
| `tools/import_bmtc.py` | new: GTFS → contract converter (Python stdlib) |
| `tools/check_bmtc.py` | new: validates the committed sample, coordinates and output blocks |
| `docs/REAL-DATA.md` | new: source, licence, selection rules, regeneration |
| `Makefile` | `make data` regenerates the BMTC sample and expected output |

The batch path is untouched behaviourally:

```bash
./build/journey < samples/example1.txt | diff - samples/expected1.txt && echo OK
```

## If you have ten minutes

1. `make && make test && make ui-test && bash tools/smoke.sh`
2. `make serve`, open <http://127.0.0.1:8080>, run the seven steps in
   `docs/DEMO-SCRIPT.md` → Act 2c.
3. Take a row from `docs/TASKS.md`: U16 (merge the redesign — P0) is the
   critical path; R1, U10, U11, U13 and U14 are open. Specifications live in
   `docs/UI-ROLES.md`.

## Known gaps

Nothing blocks the CLI demo. These are the honest weak spots:

- **Redesign branch not merged.** `ui/bmtc-redesign` has the refreshed planner
  and the real geo map, but `web/` conflicts with U4–U12/R8. Resolve by hand and
  keep both sides; do not force-push over `main`.
- **Single-threaded server.** One request at a time, loopback only. Fine for a
  demo; not a hosted service.
- **Browser automation exists but is UI-version-specific.** `make ui-test`
  checks the API; `bash tools/ui_demo_check.sh` drives Act 2c in Playwright.
  Its selectors track `ui/bmtc-redesign`, so re-run it after any `web/` merge.
- **Accessibility pass implemented (U7).** Keyboard focus, labels, ARIA and
  non-colour cues are in; an automated `axe` run is still worth doing on the
  final merged UI.
- **Touch gestures & 390 px layout added (U8).** Implemented and checked in
  Chromium at 390 px; not yet exercised on physical touch hardware.
- **Pasted networks persist locally (U9)** but the URL hash still stores only a
  sample file name, so a pasted network is not shareable by link.
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
