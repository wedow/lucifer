---
id: luc-r4xh
status: closed
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


## Notes

**2026-11-21T04:12:50Z**

The measured base (`efa5f4a9`) already stored the current context on the task and used `dict.get` in the Python method. `PyContextVar_Get` still called that method on misses. It now shares a native lookup with `ContextVar.get` and reads the call and variable defaults directly. A lookup on a fresh task leaves the context absent, and the switch watcher receives `None` when that state is restored. The active-context path uses one dict lookup without retaining cached bindings.

The new C-API kernel check failed on the base for the invalid-type error, the `Token.MISSING` default and the context installed by a fresh-thread lookup. All three contextvars kernel tests passed in plain, jit and jit-deopt. The `ready.contextvars` extension check and ABI audit passed.

All five Cadence output digests matched the CPython reference. The corpus columns use the tool's best of three after a warmup. The paired columns use the median of five alternating runs on one core after a warmup. Values are seconds with the default JIT.

| Cadence program | Corpus before | Corpus after | Paired before | Paired after |
| --- | ---: | ---: | ---: | ---: |
| continuing_brain | 3.08 | 3.25 | 3.04 | 2.77 |
| walking_rhythm | 16.81 | 15.50 | 16.54 | 12.99 |
| walking_for_reward | 20.94 | 19.29 | 20.83 | 17.70 |
| bandit | 4.59 | 4.66 | 5.41 | 4.94 |
| night_replay | 6.71 | 6.19 | 6.51 | 5.26 |
