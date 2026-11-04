---
id: luc-sx8l
status: closed
deps: []
links: [luc-njhp]
created: 2026-11-04T03:04:03Z
type: task
priority: 1
---
# Python-side compiler still 5x slower than CPython running it: kernel frames per operation

luc-njhp cut a 100,000-line function from ~720 s to ~110-145 s and a traceback from 3.07 s to 0.65 s; the targets are 60 s and 0.3 s. The compiler's own passes are linear (host CPython timing 2.5k..40k lines doubles per doubling). The remaining gap is bytecodes run by kernel code in Python: about 4.0M frames per 1000 lines against 1.7M for the same compiler under CPython. Next, by frames per 1000 lines (pair profile): Python class instantiation (type.__call__, object.__new__, object.__init__, call_special: ~150k), generic getattr/setattr through builtins getattr/setattr and getattr_hook (~150k), slice_index/slice_bounds (~70k), rich compare for bool/int mixes (compare, _entry, eq_bool: ~90k), bytearray.append (_o, _byte, _nokw: ~80k), dict.get on keys that are not str/int/class (~70k), tuple/str getitem outside the eval-loop fast path. A traceback is 18 compiles at import (12 namedtuple lambdas, 5 dataclass bodies from _colorize, ~0.5 s of the 0.65 s): per-compile cost, or caching compile() of generated source, is the lever.

## Acceptance Criteria

100,000-line function compiles under bin/lucifer in under 60 s; an uncaught exception exits in under 0.3 s; make check-timing bounds tightened to match


## Notes

**2026-11-04T15:02:28Z**

Progress on luc-sx8l: kernel frames per 1000 lines cut by about 50 percent and the 100k-line function compiles in about 52 s (target 60 s met; was ~120 s). An uncaught exception takes 0.39 s (target 0.3 s not met): its 0.33 s is the import of traceback, whose 5 dataclass bodies compile at ~14 us per source character, i.e. the Python-side parser and compiler passes themselves (Parser.expect_token, memo wrappers, opcode predicate calls), not kernel frames any more. What landed: eval-loop paths for enumerate steps, dict and view iterators, getattr/hasattr/setattr, slices and negative indexes, bool/tuple/identity equality, class instantiation (Python __init__ run in the loop; __new__ run in the loop for classes whose __init__ is object's), list/tuple of a sequence, dict.get/items, list.append, bytearray.append, isinstance over tuples, namedtuple field reads, native tuple/float/bytes/None hashing and equality in dict and set probes, and a type attribute cache keyed by type version. timing bounds tightened to 100 s and 0.8 s. Remaining lever is per-bytecode cost in the eval loop and call/return overhead; the opcode predicate functions of lib/compiler (is_jump, has_target, ...) are ~25 percent of remaining frames.
