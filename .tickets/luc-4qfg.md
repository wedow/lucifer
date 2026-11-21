---
id: luc-4qfg
status: closed
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 2
tags: [stdlib]
---
# marshal.load is missing; marshal.loads ignores allow_code

lib/modules/marshal.py has loads, dumps and (luc-kc4d) dump, but no load: pstats.Stats(filename) and test_marshal (32 ERROR in the floor) need it. CPython reads exactly the object's bytes from the file (readinto of each size), so consecutive loads and interleaved reads work (test_multiple_dumps_and_loads) and a bad readinto raises ValueError (test_bad_reader). loads(allow_code=False) must reject code objects.


## Notes

**2026-11-21T11:08:49Z**

Commit 6468e0e6 already added streaming marshal.load and the oversized readinto check. Both loading functions now honor allow_code, including for code nested in containers. loads uses the buffer protocol, and load issues the zero-length reads that CPython makes for empty bytes and ASCII strings. The new kernel regression failed on the base. It passed with the existing stream and dump checks in plain, jit and jit-deopt. The ABI audit passed.
