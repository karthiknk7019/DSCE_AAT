# Handoff — current state

Last updated: **2026-11-26, 13:40 IST**

`main` now has one merged browser UI: the printed-map redesign, the real BMTC
geo map, and every teammate feature from U4–U14/R8. The merge is `0a2ac9e`.
This page is the short version for whoever picks it up next; the detailed map is
`docs/UI-ROLES.md`, the user-facing tour is `docs/UI.md`.

## Git state

- Branch: `main`; the redesign merge is `0a2ac9e` (parents `4591382` and
  `9424cb6`), and the final integration of the R4/R6 data work is `018681d`.
  The old `ui/bmtc-redesign` branch is kept for history; do not merge it again.
- The merge kept the redesign layout/typography/geo projection and wired the
  teammate features into it: run-all queries (U4), hover highlight + tooltip
  (U3/U5), accessibility attributes (U7), pinch/tap guards (U8),
  `localStorage` for pasted networks (U9), `--threads`/`--log` (U11), SVG/PNG
  export (U12), stop search + focus bar (R8).
- One honest regression: U14's dark half. The redesign ships a single light
  theme, so `#themeBtn` is gone and U14 is `blocked` until a dark CSS-variable
  pass lands. The light tokens are still in the stylesheet.

Recent commits on `main` (newest first):

| SHA | Message |
|---|---|
| `018681d` | ui: merge teammate data/licence and refresh work into the redesign |
| `0a2ac9e` | ui: merge the planner redesign and real BMTC geo map |
| `a792a9e` | ui, data: add data/licence panel (R6) and BMTC refresh manifest check (R4) |
| `4591382` | ui, server: add light theme (U14) and opt-in --log/--threads N (U11) |
| `f254a38` | data: verify BMTC geo map, add standalone map renderer and screenshot |
| `521e437` | docs: refresh the contributor start path, task board and handoff |
| `0cab40c` | data: add real BMTC sample, importer, coverage check and geo map |
| `a35e02c` | tools: add Act 2c browser demo check (U6) |

## Verified on this machine

Run from the repo root, 2026-11-26 (CachyOS, g++ 16.2.1):

```bash
make                  # no warnings (-Wall -Wextra -Wpedantic)
make test             # 15 test functions, perf 28 ms (limit 1000 ms)
make ui-test          # API build/journey/closure/sweep/static all OK
bash tools/smoke.sh   # 5/5 sample files byte-exact, including real BMTC
python3 tools/check_bmtc.py  # OK: 400 routes, 12,325 appearances, 2,175 stops
bash tools/ui_demo_check.sh  # Act 2c steps 1-6 green on the merged tree
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
3. Take a row from `docs/TASKS.md`: R1, U10, U13 and U14 are open; U15 is
   claimed. Specifications live in `docs/UI-ROLES.md`.

## Known gaps

Nothing blocks the demo. These are the honest weak spots:

- **U14 dark theme is deferred.** The merged redesign (U16) is light-only; the
  old `#themeBtn` and dark tokens are in history but not wired into the new
  stylesheet. A dark CSS-variable pass is the remaining work.
- **Single-threaded by default.** `--threads N` exists (U11), but the default
  `--serve` path still handles one request at a time on loopback. Fine for a
  demo; not a hosted service.
- **Browser automation is UI-version-specific.** `make ui-test` checks the API;
  `bash tools/ui_demo_check.sh` drives Act 2c in Playwright. Re-run it after any
  `web/` merge — the selectors track the current markup.
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
