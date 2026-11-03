---
id: luc-66sy
status: open
deps: [luc-llzg, luc-vvqv, luc-774h, luc-e01x, luc-q6qr]
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
- Foundations (docs/FOUNDATIONS.md 2): tests/kernel/signals.py (raise_signal in a tight loop, _thread.interrupt_main, setitimer interrupting time.sleep), shutdown.py (atexit order and exceptions, __del__ at exit, sys.exit codes), imports.py (mtime pyc invalidation, CHECKED_HASH pycs, invalidate_caches, zip on sys.path, reload) and audit.py (compile/exec/open/import/sys._getframe events) match the oracle.
- tests/embed/run.c linked against build/liblucifer.a runs PyRun_SimpleString across Py_FinalizeEx and Py_Initialize twice; output matches bin/lucifer.

## Notes

**2026-11-03T02:48:33Z**

Owns uncaught-exception rendering (sys.excepthook -> traceback._print_exception_bltin, DESIGN 9.3 startup row): K1 programs end normally or by SystemExit (see luc-vvqv Bootstrap order).
