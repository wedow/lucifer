---
id: luc-msp6
status: open
deps: []
links: []
created: 2026-11-09T02:01:52Z
type: task
priority: 2
parent: luc-vxsk
tags: [perf]
---
# exceptions: raise and catch cost about 3.5x CPython

From luc-vxsk micro measurements (ns per op, CPython 3.14t / branch): try: raise KeyError(i) except KeyError: 187 / 677; a coroutine run to completion with send() caught as StopIteration 282 / 1240; getattr(dict, name) miss caught 480 / 1574. Profile of the raise loop: exception allocation and free (lu_obj_malloc, check_new, exc_dealloc -> layout_of 8%), BaseException.__new__ through h_prim_call, type_lookup. The coroutine completion StopIteration is luc-j7zk's remaining item.

