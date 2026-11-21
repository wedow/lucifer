---
id: luc-w2ra
status: open
deps: []
links: []
created: 2026-11-21T05:59:49Z
type: task
priority: 1
assignee: Greg Wedow
---
# startup: importing site costs ~91M instructions (-c pass 180M -> 271M)

23e0d389 made startup import site, as CPython does. Median user-space instructions for -c pass rose from 179.9M to 271.1M (+51%); -S stays at 179.9M. site.py and _sitebuiltins are frozen, so the cost is in what site does (imports, stat/listdir of site-packages, .pth handling, sysconfig/user-site paths). Attribute it with perf/icount, compare with CPython 3.14t's site cost on the same host, and fix the general mechanism it exposes (import machinery, os/stat paths, kernel boot), never site.py itself.


## Notes

**2026-11-21T06:32:28Z**

A pinned `perf record -e instructions:u -c 100003 --call-graph fp` run of
`-c pass` at 35089c20 attributed about 89.9M sampled instructions to the
startup import of site in `runtime/pylifecycle.c`. The largest sampled self
cost in that stack was `eval_frame` at 23.3M. Inclusive samples included
7.6M in namespace and dict lookups, 6.1M in type lookup, 4.4M in marshal
decoding and 0.6M in the stat, fstat and listdir primitives. These inclusive
counts can overlap and count user-space instructions only.

ba9bb065 is the first general change. Import locking uses the existing
task-owned recursive mutex directly, removing the second recursion counter
and the RLock wrapper calls. A release from another thread could decrement
the old counter before failing. The owner would then see an unlocked state
with a recursive hold left. The oracle regression failed on the base
and passes on the candidate. It also checks that fork preserves both
acquisitions in the child. The regression and the four nearby import,
fork and site checks each passed in plain, jit and jit-deopt.

**2026-11-21T06:40:25Z**

Five interleaved launches per arm on the test host, each pinned to a CPU
and measured with `perf stat -e instructions:u`. Values are medians in
millions of user-space instructions with `PYTHONHASHSEED=0`.

| Build | `-S -c pass` | `-c pass` | Added by site |
| --- | ---: | ---: | ---: |
| Lucifer 35089c20 | 180.226 | 271.792 | 91.566 |
| Lucifer ba9bb065 | 179.931 | 271.188 | 91.257 |
| CPython 3.14.10t | 36.074 | 53.087 | 17.013 |

The mutex change removes about 0.60M instructions from default startup,
including about 0.31M of the extra site cost. The ticket stays open for
the remaining startup cost, which is still about 5.4 times the oracle's
increment for site.

Separate diagnostic `-S -c` runs over `pass` put `import os` at +80.4M
on the Lucifer base and +14.3M on CPython. Importing `_collections_abc`
alone adds +44.9M and +8.9M respectively. These imports show the scale
of the dependency work, with the command path already present in the
diagnostic runs. One base pair with the JIT off still adds 85.1M for site.
