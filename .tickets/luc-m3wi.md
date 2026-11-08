---
id: luc-m3wi
status: open
deps: []
links: [luc-uexr, luc-p2yt, luc-9ags, luc-o8i6]
created: 2026-11-08T14:06:52Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: classes with a kernel __new__ and their own __init__ (list, dict, set, bytearray, exceptions) still go through type.__call__

luc-p2yt lowers a call of a class whose __new__ is a kernel function and whose __init__ is object's (str, int, bytes, tuple, range) to a call of __new__ with the class first. Classes with an __init__ of their own still reach h_call and type.__call__: list (pprint 600K per run), bytearray, dict, set, and exception classes called with keywords (AttributeError(name=..., obj=...): 190K per Flask census run, each also two star calls). Lower them as __new__ then __init__ under type.__call__'s made frame, as class_call does for object.__new__.

