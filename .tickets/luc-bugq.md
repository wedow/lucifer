---
id: luc-bugq
status: open
deps: []
links: []
created: 2026-11-03T09:29:21Z
type: task
priority: 3
tags: [kernel]
---
# types.FunctionType(code, globals, ...) and code.replace: function.__new__ and code.replace are absent

types.FunctionType(f.__code__, {}, 'zz') raises cannot create 'function' instances; types.coroutine(gen_func) raises AttributeError: 'code' object has no attribute 'replace' (types.py line 316). Found during luc-qiwk.

