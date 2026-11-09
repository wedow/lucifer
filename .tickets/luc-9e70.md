---
id: luc-9e70
status: closed
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


## Notes

**2026-11-08T19:44:38Z**

luc-ci4f: async_tree error exits 284K -> 6.5K per process. A coroutine's completion StopIteration through the kernel's _result/_send/coroutine.send now leaves each directly called unit as the call's result (225K such per run, no frame rebuild or eval loop); no unit is disabled since luc-xboo. What remains is the exception object itself (StopIteration made and matched per completion) and Task.__step_run_and_handle_result taking it; asyncio's Future/Task methods are no longer interpreted (a polymorphic cache's in-line read of a pending Task's class attribute took the generic path, luc-ci4f).

**2026-11-08T21:27:49Z**

Closing: no unit is disabled (luc-xboo) and the completion StopIteration no longer runs the interpreter (luc-ci4f); what is left is the exception object per completion, in luc-j7zk's terms.
