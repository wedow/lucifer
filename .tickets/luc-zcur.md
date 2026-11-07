---
id: luc-zcur
status: open
deps: []
links: []
created: 2026-11-07T13:11:11Z
type: task
priority: 2
tags: [jit, perf]
---
# JIT: class calls allocate the instance in line and inline __init__

Obj() costs about 8,500 instructions from compiled code (CPython 3.14t: 955): CALL of a class goes through h_call, type.__call__, object.__new__ and an __init__ frame. CD makes 3.1M instances and Havlak 1.4M per AWFY iteration. Lower a call of a heap type whose __new__ is object's and whose __init__ is a Python function: guard the type's version, pop the instance from the carrier's GC_PRE heap as emit_build pops tuples (preheader, inline values from the shared keys, type incref), and inline __init__ through inline_call. Escape analysis (roadmap item 6) can then make short-lived instances virtual. Found by luc-vzka's census.

