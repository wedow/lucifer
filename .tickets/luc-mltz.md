---
id: luc-mltz
status: open
deps: []
links: [luc-m3wi, luc-j2vs, luc-uexr, luc-4yfw, luc-n6yj, luc-6ljp, luc-r1pk, luc-xtmp, luc-9ags, luc-po0x, luc-o8i6]
created: 2026-11-08T20:03:49Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: keyword class calls of kernel classes (AttributeError(name=, obj=)) still go through type.__call__

luc-r1pk lowers positional calls of classes whose __new__ is a builtin base's kernel function (list, set, bytearray, exceptions). A call with keywords of such a class (the kernel's own AttributeError(text, name=..., obj=...) from _no_attribute, about 280K per Flask census run) still reaches h_call and type.__call__: bind_keywords does not bind into a **kwds parameter, which the kernel exception initializers take. Bind keywords into a made dict (or a virtual one) for kernel __init__ functions with **kwds, then reuse class_call's kernel path.

