---
id: luc-9e70
status: open
deps: []
links: []
created: 2026-11-08T01:02:06Z
type: task
priority: 2
parent: luc-0ivh
tags: [jit, perf]
---
# JIT: asyncio Task.__step disabled; coroutine completion StopIteration exits

From luc-0ivh census: in every async_tree variant 'jit: disabled Task.__step' (MAX_COMPILES), and ~228K error exits per run: coro.send() raising StopIteration on completion from kernel _send/_result in a unit with no handler, caught in interpreted Task.__step_run_and_handle_result. If __step compiled with the kernel send inlined, luc-ajdn's in-unit catch would take it. Find why __step recompiles 12 times (item 5 exit policy).

