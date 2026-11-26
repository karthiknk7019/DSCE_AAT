# UI workstream — ownership, architecture, next tasks

The browser UI is a **fourth workstream** on top of the three graded roles. It
does not change the problem's deliverable: the batch CLI still runs the same BFS
and still matches `samples/expected1.txt`. The UI exists to make that algorithm
visible in the demo and to give the three members a second, independent surface
to contribute to.

Read `docs/CONTRACT.md` first, then `docs/UI.md` for the user-facing tour. This
file is the working document: who owns what, how each piece works, and what is
left to do.

## Who owns what

The UI is split by **surface**, not by file count, so every member has a clear
piece and no two people edit the same area on the same branch.

| Surface | Files | Natural owner | Why this person |
|---|---|---|---|
| Layers + instrumentation | `include/stats.hpp`, the stats path in `src/graph.cpp` | **Role 1** | Only the search knows which route reached which layer; the minimality argument is theirs to defend |
| Entry point, parser, CLI parity | `src/main.cpp`, `web/app.js` → `parseNetworkText`, `cliText` | **Role 2** | They own the input grammar and the exact output contract |
| API tests, samples, demo | `tools/ui_smoke.sh`, `samples/`, `docs/DEMO-SCRIPT.md` Act 2c | **Role 3** | Their job is proving and selling the thing |
| HTTP + JSON server | `src/server.cpp`, `include/server.hpp` | **shared** — one driver per PR | It is plumbing; anyone can review it |
| Visual design + interactions | `web/index.html`, `web/style.css`, `web/app.js` | **shared** — one driver per PR | Design is easier to keep coherent with one pair of hands per change |

Ground rules for parallel work:

- `src/graph.cpp` is still Role 1's file. If you need a new stat, ask Role 1;
  do not add a second BFS anywhere.
- The browser never computes a journey. It sends a query to `/api/journey`.
  A JavaScript BFS is out of bounds even as a "fallback".
- `include/journey.hpp` is still frozen. The UI's extra API lives in
  `include/stats.hpp` / `include/server.hpp`.
- One driver per shared surface per branch. If two people touch `web/app.js` in
  the same hour, expect conflicts; split by function instead.

## Current state

Implemented and manually verified (2026-11-26):

- [x] `journey --serve [--port N] [--web DIR]`, loopback only
- [x] JSON API: `/api/network`, `/api/build`, `/api/journey`, `/api/impact`
- [x] route map with pan/zoom, click-to-set From/To, closure toggles
- [x] animated journey and transfer rings
- [x] BFS layer view: one ring per bus, exact per-layer counts, hover layer
- [x] "Why this is minimal" panel with the layer histogram
- [x] closure sweep: one re-query per route on the same index
- [x] recent queries, shareable URL hash for samples
- [x] network dialog: four samples + paste an unseen network
- [x] responsive down to ~390 px with touch pinch-zoom, tap-slop pan guard, and larger touch tap targets (**U8**)
- [x] hover a route in the list to highlight its path on the map (`.route-hover`, **U3**)
- [x] run all parsed `query` lines sequentially and show a clickable result table (**U4**)
- [x] `make ui-test` API smoke; screenshots in `docs/wayfinder*.png`

Verified numbers on the i7-13620H demo machine:

| Check | Result |
|---|---|
| `make` | no warnings |
| `make test` | 15 functions, perf 18 ms (limit 1000 ms) |
| `make ui-test` | build, journey, closure, sweep, static all OK |
| `bash tools/smoke.sh` | 4/4 samples byte-exact |
| `city.txt` query | 8–20 µs server-side |
| `city-large.txt` query | 0.5–1.1 ms server-side |
| 400-route closure sweep | ~168 ms for 400 re-queries |

## File map

Read these in this order if you are new to the front-end.

### `src/server.cpp` (~1,100 lines, no dependencies)

| Function | What it does |
|---|---|
| `Json` + `JsonParser` | minimal recursive-descent parser for the small API payloads |
| `build_from_request` | validates routes, calls `build_network` **once** |
| `network_json` | routes + stops/appearances + top hubs + largest routes |
| `journey_json` | legs, transfers, layers, reached stops, timing |
| `impact_json` | closes each route in turn, re-queries, reports critical/longer |
| `read_request` / `send_response` | one-request-per-connection HTTP/1.1 |
| `serve_static` | `web/` and `samples/`, rejects traversal, `.txt` only |
| `run_server` | bind, listen, sequential accept loop |

The server is single-threaded on purpose. A demo has one user; it makes the
"one index, many queries" story easy to follow and the code easy to defend.

### `web/app.js` (~2,300 lines, no framework)

| Section | Key functions |
|---|---|
| Utilities + API | `elem`, `svgEl`, `api`, `copyText`, `toast` |
| Parse + index | `parseNetworkText`, `indexNetwork`, `colorFor` |
| Layout | `forceLayout`, `fitWorldToStage`, `layoutNetwork`, `fitView` |
| Map | `drawNetworkMap`, `layoutLayers`, `drawLayerMap`, `renderMap` |
| Animation | `playJourney` |
| Panels | `renderResult`, `renderProof`, `renderResilience`, `renderImpactRows`, `renderLog` |
| Actions | `runQuery`, `toggleClosure`, `setPair`, `installNetwork`, `loadSample` |
| Wiring | `bindEvents`, `init`, `updateHash` / `readHash` |

State is one plain object (`state`) at the top. It holds the network index, the
last result, the current closures and the map view. There is no router and no
virtual DOM.

### `web/style.css`

- CSS variables at `:root` drive the whole theme; start there for a restyle.
- Layout: `.layout` is a 3-column grid; the `@media` blocks stack it.
- Map classes: `.route-path`, `.stop-node`, `.stop-label`, `.journey-line`,
  `.layer-edge`, `.lay-caption`.
- `[hidden]` is forced to `display: none` so `hidden` works on flex/grid
  elements (a real bug we hit with the map legend).

## Recipes

### Add an API endpoint

1. Write a `Json foo_json(const AppState&, ...)` in `src/server.cpp`.
2. Add a branch in `handle_request` before the static-file fallback.
3. Rebuild (`make`) and add a check to `tools/ui_smoke.sh`.
4. Document it in `docs/UI.md` → HTTP API table.
5. If it exposes a new algorithm fact, add it to `QueryStats` in
   `include/stats.hpp` and fill it in `src/graph.cpp` **once** — never in the
   server.

### Add a sample network

1. Drop `samples/<name>.txt` in the contract grammar (comments and blank lines
   allowed; optional `query` lines become demo pairs).
2. Add an entry to `SAMPLES` in `web/app.js` with title, note and route count.
3. Add a byte-exact expectation only if it is also wired into
   `tools/smoke.sh`; the UI does not need an expected file.
4. Update the sample table in `README.md`.

### Add a result panel

1. Add `<div id="myCard" hidden></div>` to `web/index.html` in the right rail.
2. Build it with DOM nodes in a `renderMyCard()` function; never interpolate
   user input into `innerHTML`.
3. Call it from `runQuery` and from `installNetwork` (reset path).
4. Add `.card` styles only if the shared card style is not enough.
5. Add a line to `docs/UI.md` → "What is on screen".

### Add a map mode

1. Extend `state.mode` and the segmented control in `index.html`.
2. Compute positions into `state.layerPositions` (or a new map), call
   `fitWorldToStage`, and store the bounds.
3. Draw in a `drawXMap(viewport)` and switch on `state.mode` inside
   `renderMap`.
4. Re-fit when the mode changes; keep the pan/zoom transform code untouched.

### Add a visual check

`tools/ui_smoke.sh` is the API-level test. For screenshots or click-throughs,
use a browser automation tool outside the build (Playwright is fine for the UI
part; the DSA build must stay dependency-free). Capture at 1512×950 and 760×1000
and compare by eye or with a screenshot differ.

## Task backlog

**Status lives in `docs/TASKS.md`; the tables below are the specification.**
Claim your row there before starting, keep it current while you work, and mark
it `done` with the verifying command when the acceptance column passes.

Priority is for the demo, not for theoretical completeness. Pick one, claim it
in `docs/TASKS.md`, and put your name in the PR title.

### P0 — do before the demo if not already done

| # | Task | Suggested owner | Acceptance |
|---|---|---|---|
| U1 | ~~Commit/push the UI workstream~~ **done in `d0594c0`** | — | — | `git status` clean; `make && make test && make ui-test` on a fresh clone |
| U2 | Rehearse Act 2c twice with the actual network | all three | no fumbling; both CLI fallback and UI path shown |

### P1 — high value, small changes

| # | Task | Size | Suggested owner | Acceptance |
|---|---|---|---|---|
| U3 | ~~Hover a route in the list → highlight its path on the map~~ **done by Role 2** | S | Role 2 | hover adds a `route-hover` class; no query fired |
| U4 | ~~"Run all queries" from a pasted file~~ **done by Role 2** | M | Role 2 | parses the `query` lines already collected, runs them sequentially, shows a result table |
| U5 | Route tooltip when hovering a path | S | Role 3 | tooltip shows name + stop count + open/closed |
| U6 | Browser automation for the 6-step demo | M | Role 3 | one command reproduces Act 2c; screenshots on failure |
| U7 | Accessibility pass | M | anyone | keyboard focus visible, dialog labelled, colour not the only signal, axe clean |
| U8 | ~~Touch pinch-zoom and larger tap targets~~ **done by Role 2** | M | Role 2 | usable at 390 px wide; no accidental pan on tap |
| U9 | Persist a pasted network in `localStorage` | S | Role 3 | reload restores name + routes; "clear" removes it |

### P2 — nice, not required

| # | Task | Size | Suggested owner | Acceptance |
|---|---|---|---|---|
| U10 | k-shortest / alternative journeys panel | L | Role 1 | new API field, tests, UI list; still BFS |
| U11 | Server request log + optional `--threads N` | M | shared | opt-in; default remains single-threaded |
| U12 | Export the map as SVG/PNG | M | Role 3 | client-side download; no new dependency |
| U13 | Deploy notes (systemd user unit, reverse proxy) | S | shared | documented only; loopback by default |
| U14 | Light theme via CSS variables | S | Role 3 | toggle persists; contrast checked |

Do **not** add: accounts, a database, a map tile service, a routing library, or
a graph library. They add setup risk and none of them help the viva.

## Definition of done for a UI PR

- [ ] `make` builds with no warnings
- [ ] `make test` passes (15 cases)
- [ ] `make ui-test` passes
- [ ] `bash tools/smoke.sh` passes
- [ ] no console errors at 1512×950 and 760×1000
- [ ] if the visual result changed, refresh the screenshot in `docs/`
- [ ] `docs/UI.md` updated for any user-facing change
- [ ] `docs/DECISIONS.md` gets a row for any non-obvious choice
- [ ] the batch CLI still matches `samples/expected1.txt`

## Pitfalls we already hit

- **Navigating to the same URL with a different hash does not reload.** While
  developing, hard-reload (`Page.reload(ignoreCache=True)` or Ctrl+Shift+R).
  This bit us once: the server had new JS, the page had the old function.
- **SVG labels scale with the group.** If the fitted scale is 0.45, a 10 px
  label renders at 4.5 px. Positions are normalised to the stage at scale 1
  (`fitWorldToStage`) so labels stay readable.
- **`hidden` does not beat `display: flex`.** The global `[hidden]` rule exists
  for this reason.
- **Large networks need a different layout.** Force layout is capped at 220
  stops; above that a sorted grid plus one route hue is readable, a force graph
  is not.
- **The reachable list is capped at 2,500 stops in JSON.** Layer counts stay
  exact and the legend discloses truncation; do not make the minimality
  argument depend on the per-stop list.
- **The server serves `samples/*.txt` only.** Anything else under `/samples/`
  is a 404 on purpose.
