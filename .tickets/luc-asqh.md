---
id: luc-asqh
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
# fast_float in C: float(str), json, csv number parsing

Pure C port of fast_float (correctly rounded, bit-identical to CPython's dtoa.c strtod) behind runtime/prim/pystrtod.c. Python's float() grammar (underscores, whitespace, inf/nan spellings, sign) and ValueError messages exactly as 3.14t. Keep dtoa.c for repr. Port fast_float's tests first, then differential + fuzz vs the oracle. Bench float() on short/long/exponent inputs and json_loads.

