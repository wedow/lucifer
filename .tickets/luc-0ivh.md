---
id: luc-0ivh
status: in_progress
deps: []
links: []
created: 2026-11-07T23:15:17Z
type: task
priority: 1
tags: [jit, perf]
---
# JIT: generators and coroutines in the optimizing tier (roadmap item 7)

Roadmap item 7. Generators, coroutines and async generators are never compiled (jit.c compile_at, ssa_build inlinable). Compile resumable bodies with one entry per yield; enter compiled generator code directly from next()/send()/throw()/FOR_ITER; StopIteration free on exhaustion; inline generator expressions and simple generators into known consumers (sum/any/all/list/tuple/join/for) with the generator object virtual when it does not escape; async def and async generators as far as sound. Census: corpus code that never compiles is 8.6% of time, generators first; Flask and Jinja render through generators.

## Acceptance Criteria

Compiled vs interpreted generator resumes in -X lucifer_jit_stats; Flask in-process routes, pyperformance generators/coroutines/async_tree*/deepcopy/genshi/mako/chameleon and cli programs timed master vs branch vs CPython 3.14t; AWFY no regression; generator difftests (gi_frame, gi_running, close/finally, throw into suspended, yield from/await chains, frame walkers during suspension) under jit always and deopt; five gates 0.


## Notes

**2026-11-08T03:02:42Z**

Branch luc-gen. Generators, coroutines and async generators compile as one resume unit per code (an entry per resume point, picked by frame->instr; locals in registers between yields, in the generator frame across one; in-line yield returns LU_JIT_YIELD). RESUME, compiled FOR_ITER/SEND/gen_send/next_item enter it directly (lu_jit_resume); compiled calls of generator functions make the generator without the interpreter (lu_jit_new_gen); known generators whose bodies run no Python are resumed in line at FOR_ITER/next_item (inline_resume; exits make the gen frame current). Resumes inside yield from/await stay in the eval loop (a unit per level made pyperformance generators 1.43x master). Also fixed: compiled CONTAINS_OP on lists now native (racy kernel path). Timing best-of-3 on the exclusive bench host, branch/master: coroutines 0.71, async_tree 0.985, async_tree_eager 0.98, generators 1.04, genshi 1.07, cli.black 1.11 (compile time 690->1148 ms, luc-k40h), others within 2%; Flask in-process 0.97-0.99; AWFY geomean 1.002. Micro: genexpr sum 547->120 ns/item (CPython 41), for over generator 102->35 (CPython 49), await chain 200->155 (CPython 112). Gates check, abi-audit, check-jit, check-harness, check-compiler all 0. Leftovers: luc-s4lm, luc-j7zk, luc-9e70, luc-pudm, luc-k40h.
