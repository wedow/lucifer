---
id: luc-j7zk
status: open
deps: []
links: []
created: 2026-11-08T01:02:06Z
type: task
priority: 2
parent: luc-0ivh
tags: [jit, perf]
---
# JIT: async generator protocol and throw()/close() in compiled code

From luc-0ivh census. async_generators still 12.8% eval_frame: kernel _asend_send/_unwrap raise StopIteration per async yield (RAISE_VARARGS error exits, 3.5M in one run), _asend_send retired after 50 leaves, CALL exits in it (3.7M), and the async-for StopAsyncIteration end (END_ASYNC_FOR in a handler). gen_throw (throw, close, athrow) is still a side exit in compiled callers (call() handles only gen_send among frame primitives).


## Notes

**2026-11-08T19:44:38Z**

luc-ci4f: kernel code compiles a bare raise and RERAISE, so _asend_send's cleanup handler no longer side-exits; a retired unit is retired once (its recursion's callers escalated it to level 25). async_generators interpreted instructions 321M -> 45M, 9.8 s -> 8.3 s (3.14t 0.38 s). Remaining: 750K SEND side exits in Tree.__aiter__ (the async for's await of asend), one StopIteration made per async yield (PyType_IsSubtype 6% of samples), gen_throw still a side exit.

**2026-11-09T04:45:21Z**

luc-vxsk: a coroutine run to completion with send() and caught as StopIteration costs 1.2 us against CPython's 0.28 (luc-msp6); the kernel no longer interns builtins.issubclass's name in every PyErr_SetObject.
