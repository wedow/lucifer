---
id: luc-7noc
status: in_progress
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 1
tags: [perf, runtime]
---
# deque: O(1) ends instead of list.pop(0)

collections.deque is a Python list, so popleft is list.pop(0), a memmove of the whole queue: 12-15% of samples in the async_tree family (asyncio's ready queue is a deque). Give deque O(1) ends with a ring buffer whose every mutation is one primitive under the deque's critical section, as 3.14t's deque methods are atomic. Census 2026-11-08 mechanism 10.


## Notes

**2026-11-08T19:52:12Z**

Landed on luc-hotc: deque storage is a native ring buffer (runtime/prim/deque.c, 14 primitives, each one critical section on the ring; mutation counter where 3.14t's deque bumps state). Index arguments past ssize now raise IndexError/OverflowError like CPython. Bench host, time per loop vs CPython 3.14t, master -> branch: async_tree 13.05 -> 10.18, async_tree_io 11.18 -> 7.57, async_tree_memoization 11.01 -> 8.93, async_tree_cpu_io_mixed 7.01 -> 5.74, async_tree_eager 13.94 -> 13.10, async_tree_tg 13.26 -> 10.30, async_tree_io_tg 10.65 -> 8.15, async_tree_eager_memoization_tg 12.60 -> 10.67 (whole branch). Test: tests/kernel/collections_deque_ring.py.
