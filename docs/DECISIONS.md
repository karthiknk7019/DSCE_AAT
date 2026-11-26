# Design decisions

One row per choice that a teammate or evaluator might question. Add a row in the
same PR that makes the choice. "Why" is the part you say out loud in the viva.

| # | Decision | Why | Alternative rejected | Owner |
|---|---|---|---|---|
| 1 | BFS layered by **boardings**, not by stops | The objective is fewest buses: every leg costs exactly 1, no matter how many stops it crosses | BFS counting stops — optimises the wrong quantity | Role 1 |
| 2 | Hand-written queue (circular array or linked) for the frontier | Course rule: no built-in shortcuts for the core problem; queue is Module 2 and gives us something to explain | `std::queue` / `std::deque` | Role 1 |
| 3 | A route is an **undirected set** of stops | The problem's example boards `R2` at 7 and alights at 6 while `R2 = [3,6,7]`; no direction or timetable data exists | Directed traversal — makes the worked example unsolvable | Role 1 |
| 4 | Index built once in `build_network` | 40,000 appearances; the spec explicitly requires repeated queries without rebuilding | Rebuilding per query — violates the requirement and is O(A) per query extra | Role 1 |
| 5 | Inactive routes filtered **per query** | Closures change between queries; the index must stay reusable | Removing routes and rebuilding — defeats decision 4 | Role 1 |
| 6 | Each route expanded at most once per query (`expanded[]`) | Overlapping services would otherwise be rescanned from every stop; bounds a query to O(A) | Rescan route stops for every reached stop — quadratic on dense networks | Role 1 |
| 7 | `unordered_map<string, vector<int>>` for stop → routes | Average O(1) lookup; hashing is Module 5 content; the interface hides it | Adjacency matrix — 40,000 stops makes it impossible; linear scan — O(A) per lookup | Role 1 |
| 8 | `include/journey.hpp` frozen, one owner per file | Three people (and three AI tools) editing in parallel otherwise conflict constantly | Shared free-for-all editing | Team |
| 9 | Output always says `buses`, exact block format | One less edge case; byte-exact `diff` tests | Pluralisation / free-form output | Role 2 |
| 10 | No tie-breaking between equally short journeys | The spec asks for a minimum, not a specific one; tests assert count + validity | Lexicographically smallest route sequence — extra work, no grading value | Role 3 |
| 11 | C++17, plain `make`, zero dependencies | Runs on any lab machine with g++; nothing to install | CMake/Conan/Boost — setup risk for no benefit | Team |

## Template for new rows

| # | Decision | Why | Alternative rejected | Owner |
|---|---|---|---|---|
| 12 | | | | |

## How to use this in the demo

When an evaluator asks "why did you do X?", answer with the **why** column, then
name the alternative you rejected. That is the difference between "the AI wrote
it" and "we chose it".
