---
id: luc-skph
status: open
deps: []
links: []
created: 2026-11-25T11:57:10Z
type: task
priority: 2
assignee: Greg Wedow
parent: luc-orn7
---
# asyncio Task step units retire: StopIteration as a result, and the _loop guard

From the item 2 work (01a9cae0), async_tree with yield from and await compiled. Two units of asyncio's pure-Python Task keep retiring:

1. Task.__step_run_and_handle_result and Task.__wakeup retire with 50 leaves in 50 entries. When a coroutine finishes, the inlined kernel _result raises StopIteration(value) (an error exit at _result@54) inside coro.send(None): 91K interpreted entries per run of async_tree "none". luc-9e70 and luc-ci4f took this exit for directly called units; the Task step still leaves. Fix candidates: take the raise in the unit when the handler is the root frame's own except StopIteration, or give the kernel a send form that returns (done, value) and does not raise, as CPython's C task uses.

2. Task.__step@179: the LOAD_ATTR guard on self._loop in the finally, read after the nested step call, fails 39K times per run, and Task.__step keeps retiring (142K interpreted entries). Find what the guard speculates on that a nested step changes.

Acceptance: both units stay compiled over an async_tree run (retired units and interpreted entries from the JIT stats, before and after); instructions on the async_tree family and coroutines; no benchmark-specific handling: the mechanism is an exception used as a return value at a call the unit made.

