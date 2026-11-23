---
id: luc-nnkf
status: open
deps: []
links: []
created: 2026-11-23T06:15:34Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: str slices and small dict literals without the C round trip

From the luc-3s7n firing count over 12 pyperformance programs (counts per benchmark loop under CPython's monitoring) and the luc-uqyj traces:

- A str slice that is kept (s[a:b] stored or passed on): tomli 1.4M per loop, html5lib 9.6K. Today about 750 instructions each, 480 of them in C (slice_unpack, PyUnicode_FromKindAndData, allocation). Estimated 420M cycles per loop on tomli.
- A small dict literal built and read: tomli 81K, genshi 48K, django 20K per loop. Today about 1480 instructions, 1190 in C (dict insert and probe, dict_dealloc). Estimated 48M and 29M cycles per loop.

Mechanisms: slice bounds computed in compiled code from the unboxed ints the site already has, and the new str made by one in-line allocation plus a copy for the compact ASCII and latin-1 kinds (other kinds, steps other than 1 and subclasses stay in C); a dict literal with constant str keys built from a prepared key table in one allocation (CPython's own split-key idea) and kept virtual when it does not escape.

Firing check first on the idiom rows and on tomli and genshi.
Acceptance: str slice and small dict idiom rows within 3x of PyPy; tomli whole-run cycles measurably down.

