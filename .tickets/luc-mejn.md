---
id: luc-mejn
status: open
deps: []
links: []
created: 2026-11-04T19:51:51Z
type: bug
priority: 4
tags: [stdlib]
---
# PickleBuffer and hash constructors: CPython's error text and no subclassing

pickle.PickleBuffer accepts subclasses (CPython: not an acceptable base type) and PickleBuffer() reports a Python __init__ arity error; hashlib constructors given a non-buffer say 'memoryview: a bytes-like object is required' where CPython says 'object supporting the buffer API required'; _locale's string calls pass a non-str non-None argument as NULL instead of a TypeError.

