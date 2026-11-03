---
id: luc-66sy
status: open
deps: [luc-llzg, luc-vvqv]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
parent: luc-nsmh
tags: [runtime, stdlib, slice]
---
# startup closure: site, importlib, re, json, sysconfig, runpy; difftest probe accepted

Python ports in lib/modules (marshal dumps, _sre from PyPy rsre, itertools, _collections, _codecs, _contextvars, _weakref, _thread, posix, time, _signal, errno, pwd, _imp, _types, gc, atexit, _string, _sysconfig), _sysconfigdata for cp314t, build/lib .pyc precompile, runtime compile() through lib/compiler (docs/DESIGN.md 3.3, 9.3).

## Acceptance Criteria

tools/difftest run --oracle $ORACLE_T --python bin/lucifer: probe accepted, 0 of 164 rows NOTRUN; probe wall time under the 5 s startup timeout.

