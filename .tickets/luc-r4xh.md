---
id: luc-r4xh
status: open
deps: []
links: []
created: 2026-11-09T05:53:29Z
type: task
priority: 1
tags: [perf, capi, numpy]
---
# perf: PyContextVar_Get runs the Python ContextVar.get, and a miss raises KeyError

Profile of the cadence corpus programs (perf, -X perf): PyContextVar_Get is 28-40% of Lucifer's time (walking_rhythm 40%, night_replay 37%, bandit 34%, continuing_brain 28%). numpy calls it on every array allocation (PyDataMem_GetHandler, the current memory handler, about 19%) and every ufunc call (_get_bufsize_errmask and fetch_curr_extobj_state, the errstate extobj, about 15%). runtime/capi/contextvars.c calls var.get through PyObject_CallMethod; lib/modules/_contextvars.py then runs _thread.get_ident (Python), a dict get on the thread map, and _data[self], which for a variable the context never set raises KeyError (PyErr_SetObject 2.5%) and deopts the compiled ContextVar.get and dict.__getitem__ each time (lu_jit_deopt 4.7%, jit stats: 8.7M raises in one walking_rhythm run). CPython's PyContextVar_Get is a cached C lookup (Python/context.c: var_cached, the HAMT). Nothing in the existing corpus calls contextvars from C per operation.

## Design

Native PyContextVar_Get: the thread's current context and its data dict reached in C (keep the current context on the task, as Context.run already reports switches through _lucifer.context_switched), a dict lookup without raising, then the default. A per-variable cache keyed by the context's data dict identity, as CPython caches by context version, makes the repeat case one compare. Measure with tools/bench/corpus --programs cadence.* before and after.

