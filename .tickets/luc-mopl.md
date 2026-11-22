---
id: luc-mopl
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: no compiles in steady state

Measured: on 14 pyperformance rows the JIT compiler is 11% and the interpreter 7% of cycles between the 4th and the 23rd value, long after timings are flat (django_template 18% + 20%, logging 17% + 12%, xdsl 16% + 10%, deepcopy 14% + 16%). A program that has reached steady speed should compile nothing and interpret nothing.

Task: record, per unit, why it was built again or retired after the third value (which guard, which assumption, which budget), across those rows; then fix the general causes found (a guard that fails forever should be compiled as a branch, not rebuilt; a retired unit's function should not fall back to the interpreter while its replacement is built). No thresholds tuned per program.

Acceptance: compiles between values 10 and 23 are zero on at least 12 of the 14 rows; compiler + interpreter share there under 3%.

