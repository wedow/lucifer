---
id: luc-ne54
status: open
deps: []
links: []
created: 2026-11-24T04:33:51Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit: cost of leaving compiled code for C and of unit returns (HELPER, RETURN, JUMP sequences)

Steady-state cycle attribution on 14 pyperformance programs (master after field kinds, lock elision, in-line release; sampled cycles by the IR instruction whose machine code the sample fell in), share of ALL steady-state cycles spent inside compiled code at these instruction kinds:

| program | HELPER | RETURN | JUMP | INCREF+DECREF |
|---|---|---|---|---|
| pprint | 17.1 | 3.6 | 5.4 | 4.8 |
| json_dumps | 14.9 | 6.0 | 1.1 | 4.3 |
| django_template | 14.8 | 5.2 | 4.9 | 4.8 |
| json_loads | 14.7 | 7.5 | 1.6 | 4.9 |
| chameleon | 14.6 | 3.9 | 6.7 | 7.6 |
| deltablue | 11.7 | 9.3 | 2.6 | 5.0 |
| genshi | 11.2 | 2.3 | 2.2 | 4.8 |
| xdsl | 10.6 | 5.3 | 3.4 | 3.4 |
| raytrace | 10.5 | 10.8 | 9.5 | 6.8 |
| deepcopy | 9.5 | 5.6 | 3.6 | 3.0 |
| go | 8.8 | 1.1 | 3.9 | 4.5 |
| richards | 8.7 | 5.0 | 6.0 | 17.9 |
| regex_v8 | 8.2 | 3.0 | 5.4 | 6.8 |
| logging | 7.4 | 5.6 | 1.7 | 3.1 |

Mean: HELPER 11.6%, RETURN 5.3%, JUMP 4.1%. These are cycles in the compiled code AROUND the call (the time inside the C helper is counted separately): publishing the frame state, saving and restoring registers, argument moves, the checks after the call (declined, error, frames, unit alive, deopt reason); a unit's return sequence (releases of locals, unlink of the frame, the result's hand-over); and register moves at jumps and loop back edges.

Task: measure first. For the hottest units of four of these programs, exact instructions executed per HELPER by helper kind (split into before-call, the checks after, and slow paths), per RETURN and per JUMP, with counts, so the fixes are chosen by number. Then, one landing each: the fixed cost of a published call (what must be written before the call and what can be written only if the callee asks: a frame state described by a constant record plus the registers already saved, rather than stored field by field); the checks after a call folded into one test of a word the callee sets; RETURN's releases and frame hand-over; phi moves at back edges (live-range splitting so loop-carried values keep one register across the loop).
Acceptance: the HELPER, RETURN and JUMP shares on those programs halved, with whole-run cycles down accordingly.

