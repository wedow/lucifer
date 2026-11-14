---
id: luc-4qfg
status: open
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 2
tags: [stdlib]
---
# marshal.load is missing; marshal.loads ignores allow_code

lib/modules/marshal.py has loads, dumps and (luc-kc4d) dump, but no load: pstats.Stats(filename) and test_marshal (32 ERROR in the floor) need it. CPython reads exactly the object's bytes from the file (readinto of each size), so consecutive loads and interleaved reads work (test_multiple_dumps_and_loads) and a bad readinto raises ValueError (test_bad_reader). loads(allow_code=False) must reject code objects.

