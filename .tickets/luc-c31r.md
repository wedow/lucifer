---
id: luc-c31r
status: open
deps: []
links: []
created: 2026-11-18T20:03:12Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: a direct call takes the number its callee made raw

Gap report 2026-11-18 mechanism #4 (escape across calls), step 1. A number a not-inlined callee computes and returns is boxed in the callee and unboxed and freed in the caller: raytrace boxes 14.8 M floats per 3+3-value run at Vector.dot's return alone, json_loads's scanner its ints. A root return of a number the unit made (an unmade VK_BOX_FLOAT/INT) becomes RETURN_RAW: a direct caller that set its kind's bit in the result word takes the raw word (LU_JIT_RETURN_RAW), any other the box. A unit returning one kind at every return publishes it with its direct entry; a call of a known callee whose unit does asks for it and takes the value through CALL_RAW (the raw word, or the object's number when the callee gave an object), so the caller's state holds a box that does not exist, its origin the object when there is one. Step 2 (tuples of an object and an int, for _json's (value, end) returns) is separate.

## Acceptance Criteria

jitcheck lane rawret (tests/kernel/jit_raw_returns.py) flat in int and float boxes; raytrace float allocations down; instructions per iteration down on raytrace/float/nbody/json_loads without losses elsewhere; floor quick, kernelcheck lanes and check-jit pass.


## Notes

2026-11-18, not landed. Built and gated (design as above, but a call asks only where the operator its value
feeds has met one kind there: a callee-summary want grew Havlak's find_loops past its budget, +2.1%). It
fires little: Vector.dot (raytrace's 14.8 M floats) returns an int or a float, so its sites never ask, and
json's scanner returns tuples; per run, ints -18 k and chaos floats -37 k. AWFY instructions flat to +0.6%
(Richards 1.006, Json 1.005, CD 1.003, Havlak 1.003), so it stays out until a step makes it pay.
Found on the way, for whoever revives it: a guard on a call's value (CALL_RAW's unbox, CALL_ITEM's) took the
CALL's own exit state, so an object it could not unbox (a big int) exited to the CALL and the interpreter made
the call again (fuzzdiff mathint_diff: random draws taken twice). The fix is lu_ev_begin(c, c->next) before
those guards (the exit resumes after the call, its value on the stack); tests/kernel/jit_raw_returns.py's
tick/once catches it in the jit lane.
