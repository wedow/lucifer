---
id: luc-3s7n
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit: stay in compiled code (keyword and star calls bound at compile time, exceptions as control flow, cells as loads, small polymorphic sites inlined)

Measured idiom rows that leave compiled code and run at or below CPython's speed: f(j, c=i) 530 cycles (PyPy 12), forwarding f(*args, **kw) 1418 (8), an exception raised and caught in the same function 1509 (37; a deopt and interpreter run per raise), isinstance(b, (C, B)) 210 (8; a tuple built and released per test, then a C subtype walk), a closure call 66 (4; each cell read is a C call), a call over three receiver classes 178 (21; a helper call and a full return instead of three guarded inlines), sum(genexpr) 268 per 100 items (39). In real programs the call path outside units (lu_jit_direct, call_argv, lu_jit_run, h_call, frame push/pop) is about 5% of steady state and type_lookup 2.8%.

Stages, one landing each, each gated on its idiom rows:
1. Calls: a call whose target is a known Python function binds positional, keyword, default and star parameters at compile time and inlines or calls the unit directly; no dict, tuple or frame is made.
2. Exceptions: a raise whose handler is in the same unit (after inlining) is a jump to the handler with the exception object made only if the handler reads it; a prim that fails inside a try reaches the handler without a deopt.
3. Cells: a cell read or write is a load or store on the cell object.
4. Polymorphic sites: a site with 2-4 receiver classes inlines each under its guard.
5. isinstance and issubclass against constant classes or a tuple of them fold to class guards.
6. Generator expressions and comprehensions consumed in the same unit run as loops.

Acceptance: each row within 3x of PyPy when its stage lands (1.5x is the epic's bar); jitcheck lanes for each shape.


## Notes

**2026-11-22 (stage 1, calls).** Firing check: binding at compile time already existed (bind/inline_call, kwcalls lane) for constant callees. The idiom rows left compiled code because the callee was not known: lu_ev_call_kw passed site=0 (no callee speculation), CALL_FUNCTION_EX had no callee feedback at all, and a closure known only by its code (LU_CALLEE_CODE) had no defaults, so `f(j, c=i)` with b defaulted could not bind even with speculation. Real programs (raytrace, deltablue, steady counts of helper calls by callee): keyword calls of unknown callees are rare there (type.__call__@392 and call_special@52, a few sites); the hot helper calls to Python functions are call_spread of forwarded star calls (raytrace `canvas.plot(x, y, *colour)` 10K per value with a real tuple tail; __init__ through type.__call__ when that is over budget), which this stage does not bind (a real tuple of unknown length).

Built: CALL_KW and CALL_FUNCTION_EX speculate on the site's callee (the interpreter notes CALL_FUNCTION_EX callees without cache words; the collector no longer clears cache words after one); a code-keyed callee's positional defaults are loaded from its tuple under a size guard and its keyword-only ones asked of its dict (h_kwdefault); a known function's keyword-only default is a constant guarded on its dict entry (in-place `f.__kwdefaults__[k] = v` mid-loop gave stale values before); a keyword call not inlined is bound here too (positional parameters only: h_call, so the emitted direct entry applies; else h_call_bound fills the frame's parameters); the census counts call_kw/call_ex/call_spread helpers as call_kw again (the kwcalls lane counted 0 since the middle end replacement). Test tests/kernel/jit_bound_calls.py (base: wrong output in the jit lane, the mid-loop kwdefaults change), jitcheck lane bound (under -X lucifer_jit=on).

Idioms, cycles / instructions per iteration, base -> new (host loaded; instructions are the steady column): kwcall 520 / 1366 -> 40 / 117 (PyPy 12); starcall 1504 / 3471 -> 70 / 196 (PyPy 8); calls 35 / 104 -> 38 / 104; closure 62 / 173 -> 68 / 173; poly 170 / 459 -> 180 / 459 (second run: calls 42 -> 33, closure 56 -> 61, poly 203 -> 197 at identical instructions).

Left: starcall's remaining cost is the cell read of g (a cell_load helper call and its release, stage 3); kwcall pays three identity guards for a code-keyed callee (code, globals, builtins) and the defaults tuple's three guards per call. A spliced real tuple of unknown length (raytrace) and receiving-side packing in out-of-line kernel forwarding still take call_spread.
