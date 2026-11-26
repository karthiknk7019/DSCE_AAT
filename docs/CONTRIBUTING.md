# Collaboration rules

Short version: **one owner per file, one branch per task, every change through
a small PR, `main` always buildable.**

## 0. Start here (new contributor, 10 minutes)

Read in this order: `README.md` → `docs/CONTRACT.md` (the exact input/output
grammar) → `docs/TASKS.md` (pick a row) → `docs/UI-ROLES.md` only if you are
touching the browser UI. `docs/DECISIONS.md` explains why the code looks the
way it does; read the rows for the area you are about to change.

Then build and run the same checks the rest of us use:

```bash
git clone https://github.com/karthiknk7019/DSCE_AAT.git
cd DSCE_AAT
make && make test && make ui-test && bash tools/smoke.sh
```

`make` and `make test` are the graded checks. `make ui-test` exercises the HTTP
API; `bash tools/smoke.sh` compares every checked-in sample byte-for-byte
(including the real BMTC network). If you touched `web/`, also run
`bash tools/ui_demo_check.sh` (needs Playwright once:
`python3 -m pip install playwright && python3 -m playwright install chromium`).

Pick work from `docs/TASKS.md`, not from memory: claim the row in the same PR
that does the work. If the row you want is taken, take the next unclaimed P0,
then P1 in ID order.

## 1. One-time setup (repo owner + each member)

Owner (`karthiknk7019`) — add the other two as collaborators:
GitHub → repo → **Settings → Collaborators → Add people** (permission: Write).
Each person must accept the email invite. Or, with `gh`:

```bash
gh api -X PUT repos/karthiknk7019/DSCE_AAT/collaborators/USERNAME \
  -f permission=push
```

Owner — protect `main`: **Settings → Branches → Add branch protection rule**:

- branch name pattern: `main`
- [x] Require a pull request before merging (approvals: **0** — see §4)
- [ ] (keep "Require status checks" off; there is no CI)

Each member:

```bash
git config user.name "Your Name"
git config user.email "you@example.com"
git clone https://github.com/karthiknk7019/DSCE_AAT.git
cd DSCE_AAT && make && make test
```

## 2. The loop

```bash
git switch main && git pull
# 0. Claim your row in docs/TASKS.md (or add one) before the first edit.
git switch -c role1/bfs          # roleN/short-topic
# ... work ...
make && make test
# If you touched src/server.cpp or web/, also:
make ui-test && bash tools/smoke.sh && bash tools/ui_demo_check.sh
git add -p                       # review your own diff before committing
git commit -m "graph: implement minimum-bus BFS"
git push -u origin role1/bfs
```

`docs/TASKS.md` is the single source of truth for who is working on what. Keep
your row `claimed` while you work and `done` (with the verifying command and
commit) when you merge. If you are idle, take the first unclaimed P0 row, then
P1 in ID order.

Then open a PR and **squash merge** as soon as `make` and `make test` pass.
Self-merge is allowed: ask the other two for a quick look when they are free,
but a review is not a gate. Delete the branch.

Branch names: `role1/...`, `role2/...`, `role3/...`, `ui/...`.
Commit messages: `<area>: what changed` in the imperative
(`cli: print transfer stops`, `tests: add overlapping-network case`).

## 3. File ownership (this is what prevents merge conflicts)

| File | Owner | Anyone else may edit only after the owner says yes in the PR |
|---|---|---|
| `include/journey.hpp` | all three | **frozen**: tell the others before changing |
| `src/graph.cpp` | Role 1 | |
| `src/cli.cpp`, `src/main.cpp` | Role 2 | |
| `tests/`, `tools/` | Role 3 | |
| `docs/DEMO-SCRIPT.md`, `docs/COURSE-MAPPING.md` | Role 3 | |
| `include/stats.hpp`, the stats path in `src/graph.cpp` | Role 1 | the BFS itself stays Role 1's |
| `src/server.cpp`, `include/server.hpp`, `web/` | shared UI surface | one driver per PR; announce in the group chat |
| `docs/UI.md`, `docs/UI-ROLES.md`, `docs/HANDOFF.md` | shared | update in the same PR as the change |
| `README.md`, `Makefile`, `docs/CONTRACT.md` | shared | edit with a PR |

Never let an AI assistant rewrite a file you do not own. If two people need the
same helper, put it in `journey.hpp` through a 3-person PR, not by copying code.

## 4. PR rules

Approval is optional — this is a time-boxed event. The author is responsible
for the checks below before self-merging.

- One concern per PR; keep it under ~200 changed lines.
- The PR template asks what you changed, how you verified it, and what the AI
  wrote. Fill all three.
- Checks before merging (author or reviewer): builds, `make test` passes,
  output matches `docs/CONTRACT.md`, no leftover `TODO`, no unrelated
  reformatting.
- Review promptly — a blocked PR is worse than an imperfect merge.
- If `main` breaks, revert the offending merge first, fix on a branch second.

## 5. AI usage policy (course allows AI; we stay accountable)

Allowed and encouraged:

- explaining errors, generating test cases, reviewing your diff
- drafting code **that you then read line by line and can defend**
- writing docs and commit messages

Not allowed:

- pasting an AI-generated BFS you cannot explain
- using a library graph/BFS implementation, or `std::queue` for the frontier
  (we hand-roll the queue — it is Module 2 content and a viva talking point)
- committing API keys, tokens or `.env` files
- letting an AI reformat or "clean up" files owned by someone else
- claiming AI work as your own in the contribution discussion

**Shortcut policy (updated):** only the DSA core is held to the hand-written
rule. The HTTP server, JSON handling, UI, test tooling and browser automation
may use pragmatic shortcuts or third-party tools, as long as `make` stays
dependency-free and `./build/journey` still matches the contract byte-for-byte.
If a shortcut changes the core, it is out of bounds.

Disclose in the PR: tool name, what it produced, what you changed after
reviewing it, and the command you ran to verify.

## 6. Definition of done (per PR)

- [ ] `make` builds with no warnings
- [ ] `make test` passes (except the intentional red tests before Role 1 lands)
- [ ] `make run` output matches `samples/expected1.txt`
- [ ] your `docs/TASKS.md` row is `done`, with the command you ran and the commit
- [ ] `docs/DECISIONS.md` updated if you made a design choice
- [ ] you reread your own diff, `make` and `make test` are green, branch deleted
