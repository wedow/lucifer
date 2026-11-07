---
id: luc-vzka
status: in_progress
deps: []
links: []
created: 2026-11-07T10:36:17Z
type: task
priority: 1
tags: [jit, perf]
---
# JIT: inline container and allocation fast paths, three-instruction poll

Roadmap item 2 (docs: jit-roadmap section 4). LIST_ITEM and tuple items read in line (bounds, load, owner incref; try-incref only on the slow path). LIST_STORE takes ob_mutex in line as INLINE_STORE does. Int and float boxes, small tuples, lists and instances allocate in line. [x] * n and BUILD_LIST lower to allocate-and-fill instead of binary_op -> list.__mul__ -> int.__rmul__. The poll shrinks to 2-3 instructions by folding the QSBR and scheduler checks into the poll word.

## Acceptance Criteria

Per-op instruction counts for list read, list store and box drop under a core lease, with cycles agreeing. AWFY Storage and Havlak faster. threads_container_protocol and the other threads_* tests stay green; make check, check-jit, abi-audit, check-harness and check-compiler pass.


## Notes

**2026-11-07T13:11:03Z**

Landed on branch luc-inline (roadmap item 2), master 033fe27 merged (luc-x8q2's polls and CHECK_LIVE compose: only LU_POLL_QSBR is handled in place, every other reason still leaves).

Census before (master, -X lucifer_jit_stats plus a throwaway counter in each helper, one AWFY iteration at the canonical inner count). Calls per iteration:

| Row | list read | list store | int box | float box | BUILD_LIST | BUILD_TUPLE | [x]*n via binary_op | class calls |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Storage | 0 | 5.46M | 36.9M (all small) | 0 | 5.47M | 0 | 5.46M | 0 |
| Havlak | 10.9M (97% owned, 3% immortal) | 5.53M | 7.21M (68% small) | 0 | 4.36M | 2.54M | 0.73M | 1.43M |
| CD | 0.43M | 0.44M | 0.98M | 8.23M (96% freelist) | 6.34M | 0 | 0.11M | 3.11M |
| Json | 0.01M | 0.47M | 1.28M | 0 | 0.86M | 0 | 0.10M | 0.37M |
| Sieve | 15.0M (immortal) | 33.2M | 2.03M | 0 | 0.01M | 0 | 0.002M | 0 |

No list read found another thread's item. After (branch), C paths per iteration: list read 0, list store 0, int box <=1.2K, float box <=2.9K, BUILD_LIST <=2.3K, BUILD_TUPLE <=0.6K on all five rows.

Per op, one core: instructions (cycles) per iteration of a loop that does the op once, the empty loop included:

| Op | CPython 3.14t | master | branch |
|---|---:|---:|---:|
| empty loop | 214 (61) | 62 (25) | 54 (22) |
| list read | 280 (85) | 93 (43) | 69 (21) |
| tuple read | 259 (76) | 91 (36) | 63 (19) |
| list store | 266 (107) | 174 (82) | 75 (60) |
| int box + store | 430 (157) | 398 (148) | 198 (90) |
| float box + store | 390 (154) | 294 (113) | 183 (86) |
| [None] * 8 | 1046 (381) | 9679 (4530) | 1141 (695) |
| (o, l) | 440 (135) | 478 (164) | 255 (87) |
| [o, l] | 509 (161) | 554 (218) | 299 (110) |
| Obj() | 955 (331) | 8612 (3480) | 8524 (3580) |

AWFY rows at reduced inner counts, one core: Storage 11.6G -> 2.08G instructions (5.38G -> 1.22G cycles), Havlak 53.1G -> 44.6G (28.0G -> 23.6G), CD 12.8G -> 12.1G (6.41G -> 6.13G), Json 4.34G -> 4.08G (2.08G -> 1.93G), Sieve 1.77G -> 0.99G (0.91G -> 0.74G). Cycles agree with instructions on every row.

AWFY panel, exclusive host, best of 3 at canonical inner counts (ms; CPython 3.14.10t, master 2f79e7b, branch 8a0f664):

| Row | CPython | master | branch | branch/master | branch/CPython |
|---|---:|---:|---:|---:|---:|
| Bounce | 1750 | 914 | 865 | 0.946 | 0.494 |
| CD | 2536 | 6694 | 6402 | 0.956 | 2.524 |
| DeltaBlue | 655 | 1288 | 1189 | 0.923 | 1.815 |
| Havlak | 3550 | 6869 | 5673 | 0.826 | 1.598 |
| Json | 903 | 2293 | 2066 | 0.901 | 2.289 |
| List | 1105 | 829 | 794 | 0.959 | 0.719 |
| Mandelbrot | 1410 | 347 | 322 | 0.929 | 0.229 |
| NBody | 1959 | 465 | 435 | 0.936 | 0.222 |
| Permute | 2088 | 956 | 750 | 0.785 | 0.359 |
| Queens | 1010 | 490 | 354 | 0.723 | 0.351 |
| Richards | 4362 | 2212 | 2091 | 0.945 | 0.479 |
| Sieve | 2174 | 880 | 677 | 0.769 | 0.311 |
| Storage | 1735 | 6185 | 1207 | 0.195 | 0.696 |
| Towers | 1416 | 678 | 547 | 0.807 | 0.386 |
| geomean | | | | 0.785 | 0.629 (master 0.802) |

Flask in process (create_app().test_client(), best of 5 x 2000 requests, us per request; CPython / master / branch): index 441 / 3225 / 3170, show 311 / 2026 / 1916, new 276 / 1813 / 1714, api 274 / 2182 / 2094, toggle 259 / 1816 / 1703 (branch 2-6% below master; the routes are call- and generator-bound).

Left: instances (Obj() costs 8.5K instructions against CPython's 955; CD makes 3.1M and Havlak 1.4M per iteration) go through h_call and type.__call__; allocating them in line needs the class call lowered (type-version guard, in-line __init__, inline-values layout): luc-zcur, next to items 4 and 6. [x] * n still snapshots the list and increfs per slot (2x CPython's cycles). luc-bbh3: an exit after a call may read a register the call clobbered (found here, not reproduced).
