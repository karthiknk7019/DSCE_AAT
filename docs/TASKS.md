# Task board — single source of truth

Every agent and teammate reads this before touching code. The **specification**
for each task (acceptance criteria, recipes, pitfalls) stays in
`docs/UI-ROLES.md`; this file records **who is doing what and whether it is
verified**.

## Rules

1. **Claim before you code.** Set the row to `claimed` and put your name or
   session in Owner *before* the first edit. A claim with no edit for a day is
   stale; unclaim it.
2. **One active row per worker.** Finish or explicitly release a row before
   starting another.
3. **Done means verified.** Mark `done` only after the acceptance column passes
   on your machine, and record the command you ran plus the commit hash.
4. **Unclaim when you stop.** If you cannot finish, set the row back to
   `unclaimed` and leave a one-line note. Do not leave silent work in progress.
5. **New work starts with a row.** If you find work that is not listed, add it
   here first, then do it. Undocumented work is invisible to the other two
   members and will be duplicated or lost.
6. **Never weaken a test or an acceptance criterion to close a row.** Fix the
   code or change the spec in a PR with a reason.

Status values: `unclaimed`, `claimed`, `blocked`, `done`.

**If you have no assigned work, take the first `unclaimed` P0 row, then P1 in
ID order. Do not wait to be asked.**

## Board

| # | Pri | Task | Status | Owner | Acceptance / evidence |
|---|---|---|---|---|---|
| U1 | P0 | Commit and push the UI workstream | done | UI workstream | `d0594c0`; fresh clone builds; `make && make test && make ui-test` |
| U2 | P0 | Rehearse Act 2c twice with the real network | unclaimed | all three | no fumbling; both the CLI fallback and the UI path shown |
| U3 | P1 | Hover a route in the list → highlight its path on the map | done | team | `698fb72` landed: hover adds `route-hover`; no query fired |
| U4 | P1 | "Run all queries" from a pasted file | done | team | `3a7a2d2` landed: sequential runner + result table |
| U5 | P1 | Route tooltip when hovering a path | done | Role 3 | `0d47e1c` landed: `makeRouteTooltip` + `pointer-events: stroke` shows name + stop count + open/closed |
| U6 | P1 | Browser automation for the Act 2c demo | done | Hermes agent | `tools/ui_demo_check.sh` drives steps 1–6 and passed against `origin/main` `7bf4d56`; failure screenshots in `build/ui-demo/`; it also caught the concurrent BMTC page error |
| U7 | P1 | Accessibility pass | done | Role 2 (karthiknk7019) | `make && make test && make ui-test && bash tools/smoke.sh && node --check web/app.js` passed; added `:focus-visible`, `<label>`s, dialog `aria-labelledby`, tablist/tabpanel arrow keys, live regions, `--dim` AA contrast, `closed` text badge, and dashed destination ring (live browser `axe` pass still pending) |
| U8 | P1 | Touch pinch-zoom and larger tap targets | done | Role 2 | `make && make test && make ui-test && bash tools/smoke.sh && node --check web/app.js` passed; two-finger pinch-zoom, 10 px touch tap-slop pan guard, and 390 px tap targets added (physical touch device not tested here) |
| U9 | P1 | Persist a pasted network in `localStorage` | done | Role 3 (Antigravity) | `savePastedNetwork`/`loadSavedPastedNetwork`/`clearSavedPastedNetwork` in `web/app.js` + `#clearPaste` in `web/index.html`; reload restores name + routes, "Clear saved" removes it |
| U10 | P2 | k-shortest / alternative journeys panel | unclaimed | Role 1 | new API field, tests, UI list; still BFS |
| U11 | P2 | Server request log + optional `--threads N` | done | Role 2 & Role 3 (karthiknk7019) | `make && make test && make ui-test` passed; opt-in `--log` and `--threads N` (`1..16`, default single-threaded), `std::shared_mutex` protecting `AppState`, verified in `tools/ui_smoke.sh` |
| U12 | P2 | Export the map as SVG/PNG | done | Role 3 (Antigravity) | `buildExportSvg`/`exportSvg`/`exportPng`/`triggerDownload` in `web/app.js`; `#exportSvgBtn` and `#exportPngBtn` in `.stage-actions`; brace/paren balance verified; SVG inlines CSS vars + bg rect; PNG rasterises via Canvas at device pixel ratio |
| U13 | P2 | Deploy notes (systemd user unit, reverse proxy) | done | Role 2 & Role 3 (karthiknk7019) | `docs/DEPLOY.md` documents `systemd --user` service, Caddy/Nginx reverse proxy, `--threads`/`--log`, and `127.0.0.1` loopback default; linked from `README.md` and `docs/UI.md` |
| U14 | P2 | Light theme via CSS variables | blocked | Role 3 (karthiknk7019) | Light tokens landed in `4591382`, but the merged redesign (U16) ships one light theme: the `#themeBtn` dark half is not wired into the new stylesheet. Needs a dark-variable pass before it can be called done again |
| U15 | P0 | Realistic large-city sample: replace the uniform lattice in `samples/city-large.txt` with a seeded generator (corridors, hubs, feeders, uneven route lengths) | claimed | Hermes (Role 3 session) | generator committed; `make && make test && bash tools/smoke.sh` byte-exact; route-length and stop-degree distributions varied; contract limits respected |

### Real-data workstream

Specification: `docs/UI-ROLES.md` → Real data workstream. The BMTC sample and
its data notes (`docs/REAL-DATA.md`) are committed on `main` (`0cab40c`). The
remaining R1 work is visual verification of the geo map on the redesign branch
(see U16); do not regenerate the sample.

| # | Pri | Task | Status | Owner | Acceptance / evidence |
|---|---|---|---|---|---|
| R1 | P0 | Verify the real BMTC map at 1512×950 and 760×1000 | claimed | concurrent agent session | 400 routes / 2,175 stops load; Jayanagar → Hebbal draws 2 legs; no console errors; screenshot updated. Note: the geo map is now merged (`0a2ac9e`); ready for the owner to close |
| R2 | P1 | Real route shapes from GTFS `shapes.txt` | unclaimed | Role 1 or UI | routes follow roads; JSON stays < 2 MB; shapes simplified and attributed |
| R3 | P2 | Scale bar, north arrow, 8 landmark labels | unclaimed | Role 3 | static map furniture; hidden outside geo mode |
| R4 | P2 | Data refresh script + checksum/provenance | done | Role 3 (karthiknk7019) | `make real-check` (`python3 tools/refresh_bmtc.py --check`) passed; `samples/bmtc.manifest.json` records SHA-256s + `ODbL 1.0` + `2026-11-26` + counts, and `tools/refresh_bmtc.py` reports UNCHANGED or unified diffs on re-import |
| R5 | P2 | Both directions per route | unclaimed | contract discussion | requires a naming rule or grammar change; do not do silently |
| R6 | P2 | Data and licence panel | done | Role 2 (karthiknk7019) | `make && make ui-test && node --check web/app.js` passed; `#dataLicence` inside `#networkDialog` renders attribution, source link, and generated date from `/data/bmtc.json`, plus `bmtc.txt` added to `SAMPLES` |
| R7 | P2 | Geo coverage test | done | Role 3 | `python3 tools/check_bmtc.py` → OK (400 routes, 12,325 appearances, 2,175 stops, 4 queries, 4 output blocks); committed at `0cab40c` |
| R8 | P2 | Dense-map keyboard stop search | done | Role 2 (karthiknk7019) | `make && make test && make ui-test && bash tools/smoke.sh && node --check web/app.js` passed; `#stopSearch` ARIA combobox (`F` shortcut, Up/Down/Enter/Shift+Enter/Esc), `#focusOverlay` O(1) pulse/ring pan+zoom, and `#focusedStopBar` Set From/To actions |

### UI refresh branch (merge needed)

| # | Pri | Task | Status | Owner | Acceptance / evidence |
|---|---|---|---|---|---|
| U16 | P0 | Merge the planner redesign + real BMTC geo map | done | Hermes agent | `0a2ac9e` merged into `main`, final integration `018681d`; `make`, `make test`, `make ui-test`, `bash tools/smoke.sh` 5/5, `make real-check`, `python3 tools/check_bmtc.py` and `bash tools/ui_demo_check.sh` all pass; BMTC loads 400 routes/2,175 stops at 1512×950 and 760×1000 with no console errors |
| U17 | P1 | Automated CLI contract test suite (`tools/cli_test.sh` + `make cli-test`) | done | Role 2 & Role 3 (karthiknk7019) | `make cli-test` passed (15/15 checks: batch stdin, file arg, `--interactive`, `--help`, invalid flags/threads, malformed network/query errors, unknown inactive route warnings, exit codes) |

## Notes

- The BMTC sample (`samples/bmtc.txt`, `samples/expected-bmtc.txt`,
  `tools/import_bmtc.py`, `web/data/bmtc.json`, `docs/REAL-DATA.md`) is on
  `main` at `0cab40c`. `R1` still needs the redesigned geo map (U16) before it
  can be marked done.
- When a row is `done`, keep it in the table for one release cycle, then move
  it to an archive section at the bottom rather than deleting the evidence.
