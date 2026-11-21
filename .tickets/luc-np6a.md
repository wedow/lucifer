---
id: luc-np6a
status: closed
deps: []
links: []
created: 2026-11-21T09:52:03Z
type: task
priority: 1
assignee: Greg Wedow
---
# startup: name lookups take ~5 layers per probe for exact-str keys

luc-w2ra attribution (11-21): global_get and dict probes cost ~43M instructions of -S -c pass vs ~10M in CPython. Each module-level name load goes through about five layers per probe. Add a tight lookup path for exact-str keys with a cached hash (as CPython's unicode-keys dict lookup does), used by global/builtin/name loads and the kernel's namespace lookups. Measure -S -c pass, -S -c 'import os' and a module-level name-load loop with instruction counts before and after.


## Notes

**2026-11-21T11:46:12Z**

The namespace lookup in `runtime/object/dict.c` reads the cached hash
and probes Unicode entries directly. It keeps the existing locked retry
when a concurrent change prevents taking a reference. Bytecode loads use
ordinary dict equality for mixed-key tables, and compiled loads leave
for the interpreter when a comparison needs Python.

`tests/kernel/namespace_name_lookup.py` failed on 2c30eeff because mixed
namespace keys comparing equal to the name were ignored, including
comparisons that raised KeyError. The regression and ten nearby checks
passed in each of plain, jit and jit-deopt.

Medians of five pinned launches per arm on the test host, in millions of
user-space instructions with `PYTHONHASHSEED=0`. Both Lucifer builds used
the same checkout and executable path.

| Command | Base 2c30eeff | Candidate | CPython 3.14.10t |
| --- | ---: | ---: | ---: |
| `-S -c pass` | 146.070 | 136.561 | 36.085 |
| `-S -c 'import os'` | 227.662 | 218.176 | 50.393 |
| `-S -c` module-level name loads | 996.244 | 662.190 | 481.790 |

The loop binds `left = 1` and `right = 2`, then reads `left`, `right`
and builtin `len` on each of 300,000 iterations.
