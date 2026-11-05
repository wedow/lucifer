---
id: luc-jeec
status: open
deps: []
links: []
created: 2026-11-04T21:32:01Z
type: task
priority: 0
assignee: Greg Wedow
parent: luc-d5u1
---
# P0b: object layout, allocation and lazy frames under the JIT; Bounce/Richards >=1.5x and NBody >=3x faster than CPython 3.14t, else revisit the design

Spike luc-c1cx (docs/jit-spike.md): JIT equals CPython on Sieve/NBody and is ~2.4x slower on Bounce/Richards; compiled code is 15-27% of samples. In order: (1) shared-key instance layout with inline values and a keys-version guard; (2) inline fast paths for list stores, allocation and float boxing; (3) lazy frame materialization so boxes stay virtual across calls. Bar: Bounce and Richards >=1.5x faster than CPython 3.14t, NBody >=3x, on an idle host. Missing the bar reopens the design. Start after luc-h4gc lands (both touch object layout).


## Notes

**2026-11-05T03:59:48Z**

Include luc-jh5s: on havlak the default JIT (18.7 s) is slower than -X lucifer_jit=off (12.5 s; CPython 3.14 17.4 s) — 20M side exits/run, mostly two opcodes. The JIT must never lose to the interpreter; measure off vs on for every AWFY program in this round.
