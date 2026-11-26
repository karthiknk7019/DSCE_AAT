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
| U6 | P1 | Browser automation for the Act 2c demo | claimed | Hermes agent | one command reproduces Act 2c; screenshots on failure |
| U7 | P1 | Accessibility pass | done | Role 2 (karthiknk7019) | `make && make test && make ui-test && bash tools/smoke.sh && node --check web/app.js` passed; added `:focus-visible`, `<label>`s, dialog `aria-labelledby`, tablist/tabpanel arrow keys, live regions, `--dim` AA contrast, `closed` text badge, and dashed destination ring (live browser `axe` pass still pending) |
| U8 | P1 | Touch pinch-zoom and larger tap targets | done | Role 2 | `make && make test && make ui-test && bash tools/smoke.sh && node --check web/app.js` passed; two-finger pinch-zoom, 10 px touch tap-slop pan guard, and 390 px tap targets added (physical touch device not tested here) |
| U9 | P1 | Persist a pasted network in `localStorage` | done | Role 3 (Antigravity) | `savePastedNetwork`/`loadSavedPastedNetwork`/`clearSavedPastedNetwork` in `web/app.js` + `#clearPaste` in `web/index.html`; reload restores name + routes, "Clear saved" removes it |
| U10 | P2 | k-shortest / alternative journeys panel | unclaimed | Role 1 | new API field, tests, UI list; still BFS |
| U11 | P2 | Server request log + optional `--threads N` | unclaimed | shared | opt-in; default remains single-threaded |
| U12 | P2 | Export the map as SVG/PNG | done | Role 3 (Antigravity) | `buildExportSvg`/`exportSvg`/`exportPng`/`triggerDownload` in `web/app.js`; `#exportSvgBtn` and `#exportPngBtn` in `.stage-actions`; brace/paren balance verified; SVG inlines CSS vars + bg rect; PNG rasterises via Canvas at device pixel ratio |
| U13 | P2 | Deploy notes (systemd user unit, reverse proxy) | unclaimed | shared | documented only; loopback by default |
| U14 | P2 | Light theme via CSS variables | unclaimed | Role 3 | toggle persists; contrast checked |
| U15 | P0 | Realistic large-city sample: replace the uniform lattice in `samples/city-large.txt` with a seeded generator (corridors, hubs, feeders, uneven route lengths) | claimed | Hermes (Role 3 session) | generator committed; `make && make test && bash tools/smoke.sh` byte-exact; route-length and stop-degree distributions varied; contract limits respected |

### Real-data workstream

Specification: `docs/UI-ROLES.md` → Real data workstream. The BMTC sample and
its data notes (`docs/REAL-DATA.md`) are being prepared in a concurrent session
and are **not on `main` yet**; do not duplicate that work.

| # | Pri | Task | Status | Owner | Acceptance / evidence |
|---|---|---|---|---|---|
| R1 | P0 | Verify the real BMTC map at 1512×950 and 760×1000 | claimed | concurrent agent session | 400 routes / 2,175 stops load; Jayanagar → Hebbal draws 2 legs; no console errors; screenshot updated |
| R2 | P1 | Real route shapes from GTFS `shapes.txt` | unclaimed | Role 1 or UI | routes follow roads; JSON stays < 2 MB; shapes simplified and attributed |
| R3 | P2 | Scale bar, north arrow, 8 landmark labels | unclaimed | Role 3 | static map furniture; hidden outside geo mode |
| R4 | P2 | Data refresh script + checksum/provenance | unclaimed | Role 3 | rerun produces identical sample or reports a diff; licence/date recorded |
| R5 | P2 | Both directions per route | unclaimed | contract discussion | requires a naming rule or grammar change; do not do silently |
| R6 | P2 | Data and licence panel | unclaimed | Role 2 | attribution + source link + generated date visible in the dialog |
| R7 | P2 | Geo coverage test | unclaimed | Role 3 | every stop in `bmtc.txt` exists in `bmtc.json`; every query has a journey; smoke includes the expected output |
| R8 | P2 | Dense-map keyboard stop search | unclaimed | Role 2 | type a stop instead of hunting 2,175 dots; focus and selection accessible |

## Notes

- The BMTC sample (`samples/bmtc.txt`, `samples/expected-bmtc.txt`,
  `tools/import_bmtc.py`, `web/data/bmtc.json`, `docs/REAL-DATA.md`) exists only
  in a concurrent session's working tree; R1 is the remaining blocker before it
  is committed and counts as done.
- When a row is `done`, keep it in the table for one release cycle, then move
  it to an archive section at the bottom rather than deleting the evidence.
