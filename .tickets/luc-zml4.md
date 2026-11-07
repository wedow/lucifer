---
id: luc-zml4
status: open
deps: []
links: []
created: 2026-11-07T21:12:05Z
type: task
priority: 2
assignee: Greg Wedow
parent: luc-vm3a
tags: [native, glm]
---
# simdjson in C, adapted to Python's json error model

Pure C port of simdjson's structural indexing (stage 1) and a stage 2 that builds Python objects directly, for lib/modules/_json.py's scanner. Must match 3.14t exactly: NaN/Infinity/-Infinity, arbitrary-precision ints, duplicate keys (last wins), strict control-char rules, surrogate escapes, JSONDecodeError msg/pos/lineno/colno, recursion limits. object_hook/object_pairs_hook/parse_float/parse_int/parse_constant fall back to the Python path (or call the hooks from the native stage 2 if simple). Port simdjson's test corpus (and JSONTestSuite) first with Python's expected outcomes from the oracle, then Lib/test test_json, then differential + fuzz. Encoder (dumps) string escaping via the same SIMD scan. Bench pyperformance json_loads/json_dumps and AWFY Json.

