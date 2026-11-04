---
id: ln-sx8l
status: open
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

