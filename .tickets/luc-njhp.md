---
id: luc-njhp
status: open
deps: []
links: []
created: 2026-11-03T23:49:57Z
type: bug
priority: 2
---
# Compiler is superlinear on huge functions: a 100,000-line function takes ~12 min vs 1.8 s on CPython

Found by luc-2vif: test_sys_settrace's test_jump_extended_args_for_iter execs a 100k-line function, so the module times out at 180 s. Profile lib/compiler on a generated 100k-line function; find the quadratic pass. Byte-identity with CPython must hold (make check-compiler).


## Notes

**2026-11-04T01:44:43Z**

luc-4nwl: every uncaught-exception traceback takes ~3 s, because traceback display runs the Python-side compiler, which allocates tens of millions of objects. Same root (compiler speed); it dominates kernel lane time.
