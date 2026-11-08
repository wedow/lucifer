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

