---
id: luc-m3wi
status: closed
deps: []
links: [luc-uexr, luc-p2yt, luc-9ags, luc-o8i6, luc-mltz, luc-j2vs, luc-4yfw, luc-n6yj, luc-6ljp, luc-r1pk, luc-xtmp, luc-po0x]
created: 2026-11-08T14:06:52Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: classes with a kernel __new__ and their own __init__ (list, dict, set, bytearray, exceptions) still go through type.__call__

luc-p2yt lowers a call of a class whose __new__ is a kernel function and whose __init__ is object's (str, int, bytes, tuple, range) to a call of __new__ with the class first. Classes with an __init__ of their own still reach h_call and type.__call__: list (pprint 600K per run), bytearray, dict, set, and exception classes called with keywords (AttributeError(name=..., obj=...): 190K per Flask census run, each also two star calls). Lower them as __new__ then __init__ under type.__call__'s made frame, as class_call does for object.__new__.


## Notes

**2026-11-08T22:04:06Z**

luc-r1pk: a positional call of a class whose __new__ is its builtin base's kernel function (list, set, bytearray, exceptions, subclasses) calls that __new__ (an error exit raises at the call) and inlines __init__ (Python, or the base's kernel one under type.__call__'s made frame after call_special), or calls it from that frame; only at sites that have run (an exception class on an inlined error path kept AWFY NBody's floats boxed, 1.29x). Generic class calls from compiled code (allocation census class_call): raytrace 0.28M -> 184, deltablue 79K -> 184, async_tree_cpu_io_mixed_tg 1.12M -> 0.54M, flask 0.64M -> 0.41M. Keyword calls (AttributeError(name=, obj=)) are luc-mltz. Lane jitcheck kclasses.
