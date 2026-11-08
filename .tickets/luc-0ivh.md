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

