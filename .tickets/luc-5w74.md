---
id: luc-5w74
status: open
deps: [luc-f40q, luc-616t]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1l: generators, coroutines, async generators and deep recursion

Generator, coroutine and async-generator objects with the frame embedded (owner generator); a control primitive resumes by returning the embedded frame for the loop to push, so FOR_ITER over a generator, SEND, yield from, await, next(), send, throw and close never recurse in C; gi_frame/gi_running/gi_suspended/gi_yieldfrom and cr_*/ag_* attributes; StopIteration value plumbing and PEP 479 conversion; close() and GeneratorExit; async generator asend/athrow/aclose awaitables and the firstiter/finalizer hooks in the task; coroutine wrappers for manual driving. Opcodes: RETURN_GENERATOR, YIELD_VALUE, SEND, END_SEND, GET_YIELD_FROM_ITER, GET_AWAITABLE, GET_AITER, GET_ANEXT, END_ASYNC_FOR, CLEANUP_THROW, CALL_INTRINSIC_1 (ASYNC_GEN_WRAP, STOPITERATION_ERROR). tests/kernel/deep_recursion.py is written first and shown passing on the oracle under 512 KiB.
Implements: DESIGN 5.2 (generator resumption, depth accounting), 5.8 (deep recursion); FOUNDATIONS 2.2 (generator frames).
Files: runtime/interp/gen.c, lib/kernel/generators.py.
Adds tests/kernel: generators.py, coroutines.py, deep_recursion.py (150,000-deep recursion under setrecursionlimit(200_000), 10,000-deep yield-from chain, an exception unwound through 100,000 frames).

Builder: opus.

## Acceptance Criteria

make check-kernel PROGRAMS="generators coroutines deep_recursion" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).

