---
id: luc-rwdf
status: closed
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: class calls the lowering leaves to type.__call__

luc-zcur lowers CALL of a class whose metaclass is exactly type, whose __new__ is object's and whose __init__ is a Python function (or object's with no arguments). Left to h_call and type.__call__ (about 8.5K instructions each): calls with keywords (CALL_KW), classes with a metaclass that does not override __call__ (ABCMeta, enum and dataclass-free ORMs), and classes reached through a value the builder does not know (a class in a local or an attribute). An __init__ that cannot inline (keyword-only parameters, too large, or one that kept leaving: lu_jit_noinline) is called from type.__call__'s made frame instead. The Flask census still shows 251 class calls per request through h_call.


## Notes

**2026-11-08T14:34:27Z**

Done in luc-p2yt: CALL_KW of a class whose __init__ takes the keywords by name inlines as CALL does; classes of a metaclass whose __call__ is type's (ABCMeta) lower too, under its version; a class whose __new__ is a kernel function of its own and whose __init__ is object's (str, int, bytes, tuple, range) calls __new__ with the class first. Class calls through h_call per run, master -> branch: Flask 1.30M -> 856K (120 -> 79 per request), mako 540K -> 339, dulwich_log 917K -> 31K. Left: classes with a kernel __new__ and their own __init__ (list, dict, bytearray, exceptions with keywords): luc-m3wi.
