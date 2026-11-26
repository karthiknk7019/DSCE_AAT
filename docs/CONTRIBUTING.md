# Collaboration rules

Short version: **one owner per file, one branch per task, every change through
a reviewed PR, `main` always buildable.**

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
- [x] Require a pull request before merging
- [x] Require approvals: **1**
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
git switch -c role1/bfs          # roleN/short-topic
# ... work ...
make && make test
git add -p                       # review your own diff before committing
git commit -m "graph: implement minimum-bus BFS"
git push -u origin role1/bfs
```

Then open a PR, request review from the other two, and **squash merge** after
one approval. Delete the branch.

Branch names: `role1/...`, `role2/...`, `role3/...`.
Commit messages: `<area>: what changed` in the imperative
(`cli: print transfer stops`, `tests: add overlapping-network case`).

## 3. File ownership (this is what prevents merge conflicts)

| File | Owner | Anyone else may edit only after the owner says yes in the PR |
|---|---|---|
| `include/journey.hpp` | all three | **frozen**: needs 3 approvals |
| `src/graph.cpp` | Role 1 | |
| `src/cli.cpp`, `src/main.cpp` | Role 2 | |
| `tests/`, `tools/` | Role 3 | |
| `docs/DEMO-SCRIPT.md`, `docs/COURSE-MAPPING.md` | Role 3 | |
| `README.md`, `Makefile`, `docs/CONTRACT.md` | shared | edit with a PR |

Never let an AI assistant rewrite a file you do not own. If two people need the
same helper, put it in `journey.hpp` through a 3-person PR, not by copying code.

## 4. PR rules

- One concern per PR; keep it under ~200 changed lines.
- The PR template asks what you changed, how you verified it, and what the AI
  wrote. Fill all three.
- Reviewer checks: builds, `make test` passes, output matches
  `docs/CONTRACT.md`, no leftover `TODO`, no unrelated reformatting.
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

Disclose in the PR: tool name, what it produced, what you changed after
reviewing it, and the command you ran to verify.

## 6. Definition of done (per PR)

- [ ] `make` builds with no warnings
- [ ] `make test` passes (except the intentional red tests before Role 1 lands)
- [ ] `make run` output matches `samples/expected1.txt`
- [ ] `docs/DECISIONS.md` updated if you made a design choice
- [ ] one teammate approved, branch deleted
