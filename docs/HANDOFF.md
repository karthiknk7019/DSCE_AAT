# Handoff — current state

Last updated: **2026-11-26, 12:25 IST**

The browser UI workstream is functionally complete and verified. This page is
the short version for whoever picks it up next; the detailed map is
`docs/UI-ROLES.md`, the user-facing tour is `docs/UI.md`.

## Git state

- Branch: `main`, synced with `origin/main`; the UI workstream is
  `d0594c0 ui: add browser front-end, BFS layer view and closure sweep`.
- It was rebased over two teammate commits: `84e9634` (interactive route
  details) and `cc67512` (hand-written `StopIdMap`). The only source conflict
  was `src/graph.cpp`: the `find_journey_stats` hooks were re-applied on top of
  `StopIdMap`, keeping the compact-ID BFS and the optional stats path. Both
  perf rows in `docs/DECISIONS.md` were kept.
- `5edc520` added deterministic lexicographic tie-breaking. It changes *which*
  optimal journey is printed, never the bus count, so
  `samples/expected-city*.txt` were regenerated and the demo route references
  updated from `365` to `210`.
- Latest verification: `make` clean, `make test`, `make ui-test`, and
  `bash tools/smoke.sh` 4/4 all pass; the 5,000-network oracle also passes.

## Verified on this machine

Run from the repo root, 2026-11-26:

```bash
make            # g++ 16.2.1, -Wall -Wextra -Wpedantic: no warnings
make test       # 15 test functions, perf 18 ms (limit 1000 ms)
make ui-test    # API build/journey/closure/sweep/static all OK
bash tools/smoke.sh   # 4/4 sample files byte-exact
```

Browser checks (manual, Chromium at 1512×950, 1512×1080 and 760×1000):

- no console errors;
- city demo loads and queries Jayanagar → Whitefield in **2 buses**;
- closing `V1` → **No journey**; reopening restores it;
- closure sweep reports **1 critical route** (`V1`) for that pair;
- BFS layers view places the destination on ring 2 and counts 1 / 6 / 4;
- `samples/demo.txt` → A → F is 2 buses over 10 stops;
- `samples/city-large.txt` loads ~3 s including drawing; one query is
  0.5–1.1 ms server-side; 400-route sweep is ~168 ms;
- pasting a malformed network reports the exact bad line;
- zero-bus journey, unknown stop and no-route states all render correctly.

Screenshots: `docs/wayfinder.png` (route map) and
`docs/wayfinder-layers.png` (BFS layers).

## What changed

| File | Change |
|---|---|
| `include/stats.hpp` | new: `QueryStats`, `find_journey_stats` |
| `src/graph.cpp` | one shared BFS with an optional stats pointer; `find_journey` is a wrapper with `nullptr` |
| `include/server.hpp` | new: `ServerOptions`, `run_server` |
| `src/server.cpp` | new: POSIX HTTP + JSON API, static files, build/journey/impact handlers |
| `src/main.cpp` | new `--serve [--port N] [--web DIR]`; help text |
| `Makefile` | links `src/server.cpp`, `serve` and `ui-test` targets |
| `tests/test_graph.cpp` | case 15: stats agree with the search (layers, skipped closures, zero-bus) |
| `tools/ui_smoke.sh` | new: end-to-end HTTP smoke check |
| `web/index.html`, `web/app.js`, `web/style.css`, `web/favicon.svg` | new: the UI |
| `README.md` | UI section, screenshot, quick start, repo map, status |
| `docs/UI.md` | new: UI tour, API, layer view, demo path |
| `docs/UI-ROLES.md` | new: ownership, architecture, recipes, backlog |
| `docs/HANDOFF.md` | new: this page |
| `docs/CONTRACT.md` | documents `--serve` and `find_journey_stats` as additive |
| `docs/DECISIONS.md` | rows 17–24 + measured perf log |
| `docs/DEMO-SCRIPT.md` | Act 2c (browser demo), question bank, fallback |
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
3. Pick a small backlog item from `docs/UI-ROLES.md` (U3, U5 and U9 are small).

## Known gaps

Nothing blocks the demo. These are the honest weak spots:

- **Single-threaded server.** One request at a time, loopback only. Fine for a
  demo; not a hosted service.
- **Browser automation is manual.** `make ui-test` checks the API, not pixels.
  Task U6 in `docs/UI-ROLES.md` covers adding Playwright for the UI part.
- **Accessibility is not audited.** Keyboard entry works (`Enter`, `S`, `/`),
  focus styles exist, but there has been no axe/Screen-reader pass (U7).
- **Touch pinch-zoom is not implemented.** Drag and wheel work; small screens
  are readable but not gesture-polished (U8).
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
