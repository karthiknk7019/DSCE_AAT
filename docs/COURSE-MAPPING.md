# Course mapping (1BCS305)

Why this project is a data-structures project and not just "a program that
works". Use this to answer "which part of the syllabus is this?" — the course
handout is in [`../1BCS305_DSA.pdf`](../1BCS305_DSA.pdf).

## Where each course concept appears

| Course concept | Module | In this project | One-line explanation |
|---|---|---|---|
| Queue ADT (circular / linked) | 2 | BFS frontier in `src/graph.cpp` | "Stops are enqueued when discovered and dequeued in discovery order; that order is what makes BFS layering correct." |
| Linked list / dynamic memory | 1–2 | queue implementation, `std::vector` route storage | "We hand-rolled the queue nodes/ring instead of using `std::queue`." |
| Graph representation — adjacency list | 5 | `Network::stop_to_routes` | "It is the adjacency list of a bipartite stop↔route graph: one side stops, other side routes." |
| BFS | 5 | `find_journey` | "One BFS layer = one bus boarding; the first layer that reaches the destination is the minimum." |
| Hashing | 5 | `unordered_map` stop → route indices | "Average O(1) lookup keeps a query proportional to appearances, not to a linear scan of stops." |
| Complexity analysis | 1, 5 | comments + `docs/DECISIONS.md` | "Build O(A), query O(A), memory O(A), A = stop appearances ≤ 40,000." |
| Self-study: graph applications (route planning, recommendation) | syllabus | the whole problem | "Problem statement 10 is the route-planning application named in the self-study list." |

## What we deliberately did **not** use, and why

| Structure | Why not |
|---|---|
| DFS | Does not guarantee the minimum number of buses |
| Dijkstra / priority queue | All buses cost exactly 1, so BFS is the correct special case; Dijkstra would add a heap for no benefit |
| Adjacency matrix | 40,000 stop appearances — memory blow-up, and most pairs are empty |
| BST / AVL / heap / trie | No ordered search, priority ordering, or prefix queries in this problem; using them would be decoration |
| `std::queue` | Course rule against built-ins for the core; we implement the queue ourselves |
| External graph library | Same rule; also not available on lab machines |

## Allowed toolbox

- `std::vector`, `std::string`, `std::unordered_map`, `std::istringstream`,
  `std::chrono` (tests only), `std::sort`/`std::find` for non-core bookkeeping.
- Hand-written: BFS queue, graph index, search and reconstruction.
- If an evaluator wants visible Module-5 hashing, replace the
  `unordered_map` with an open-addressing or separate-chaining table behind the
  same `Network` field. The rest of the code, and all tests, stay unchanged.

## Individual contribution — how to phrase it

Fill this in for yourself before the demo:

> "I implemented **\<what\>** in **\<file:line\>**. It uses **\<concept\>** from
> Module **\<n\>**. The key choice was **\<decision\>** because **\<why\>**. The
> complexity is **\<...\>**. I verified it with **\<test/command\>**, and the
> result was **\<number/pass\>**."

Example (Role 1):

> "I implemented the minimum-bus BFS in `src/graph.cpp`. It uses the queue ADT
> from Module 2 and BFS over an adjacency list from Module 5. The key choice was
> to expand each route at most once per query, because overlapping routes would
> otherwise be rescanned from every stop; that keeps a query at O(A). I verified
> it with the problem example and the dense-network perf test at 40,000
> appearances, which answers every query in a few milliseconds."

## Likely viva questions, by module

| Question | Answer lives in |
|---|---|
| "Show me your queue. Why not `std::queue`?" | `src/graph.cpp`, decision 2 |
| "Why does BFS give the minimum buses?" | `docs/ROLES.md` → Role 1, "Why this is optimal" |
| "What is your graph representation?" | `Network::stop_to_routes`, decision 7 |
| "What is the complexity?" | `docs/CONTRACT.md` §6, decision 6 |
| "How do closures work without rebuilding?" | decision 5, inactive filter in `find_journey` |
| "Where is the hashing?" | decision 7, `docs/CONTRACT.md` §5 |
