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

## 2026-11-25, after yield from and await compile

From the item 2 work (01a9cae0), measured on async_generators after yield from, await and async for compile:
- the kernel's Python asend step is the cost: lu_field_offset 6.1%, h_prim_call 4.5%, prim_gen_flag 2.2% of cycles;
- every async for ends with StopAsyncIteration raised in _asend_step@362 (699K per run), which leaves the unit for the interpreter's END_ASYNC_FOR handler; the jitcheck lane asyncfor is listed as not built for this reason.
Both belong to this ticket: handlers in generator frames taken in the unit (the end of async for stays compiled), and the asend step's generator-flag and field reads lowered or the step given a native form. Also not built: the END_SEND entry after a real suspension, so code after an await that truly suspended runs interpreted until the next plain-yield resume or a hot loop.
