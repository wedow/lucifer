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

