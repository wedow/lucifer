---
id: luc-njhp
status: open
deps: []
links: [luc-sx8l]
created: 2026-11-03T23:49:57Z
type: bug
priority: 1
---
# Compiler is superlinear on huge functions: a 100,000-line function takes ~12 min vs 1.8 s on CPython

Found by luc-2vif: test_sys_settrace's test_jump_extended_args_for_iter execs a 100k-line function, so the module times out at 180 s. Profile lib/compiler on a generated 100k-line function; find the quadratic pass. Byte-identity with CPython must hold (make check-compiler).


## Notes

**2026-11-04T01:44:43Z**

luc-4nwl: every uncaught-exception traceback takes ~3 s, because traceback display runs the Python-side compiler, which allocates tens of millions of objects. Same root (compiler speed); it dominates kernel lane time.

**2026-11-04T03:04:08Z**

Profiled: compiler passes are linear (host: 2.5k..40k lines doubles per doubling, 0.23 ms/line); the cost was kernel Python under the eval loop (15k frames per source line vs 1.7k under CPython), and tracebacks import traceback -> _colorize -> dataclasses/inspect, whose 18 exec'd sources compiled in 2.9 s. Landed on branch luc-njhp: STORE_ATTR/LOAD_ATTR slot and dict specializations, native member/getset access, class-attribute and __class__ shortcuts, Python data-descriptor __get__ run from LOAD_ATTR, native dict find/put for str/int/class keys (subscript, contains, store, dict.get), set containment, bool(), len(), isinstance(), bytes index, native setattr, dict/seq iterator steps and zip/enumerate without StopIteration, leaner AST init, tuplegetter, opcode predicate sets. Result: 6.9 -> 1.1 ms/line (100k lines: ~720 s -> 115-145 s, oracle 0.95 s); traceback 3.07 -> 0.65 s. Acceptance (60 s, 0.3 s) NOT met; remainder is ticket luc-sx8l. make check-timing and tests/kernel/huge_function.py added.
