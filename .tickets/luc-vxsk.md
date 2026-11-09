---
id: luc-vxsk
status: in_progress
deps: []
links: []
created: 2026-11-09T00:14:12Z
type: epic
priority: 1
tags: [perf, jit, web, asyncio]
---
# web and async: Flask and the async_tree family, profile then general fixes

Flask (werkzeug + Jinja2) runs about 5x CPython 3.14t's time per request in process; the 16 pyperformance async_tree variants (29.5% of the corpus's log-pull) run 5-12x. Profile both on master by mechanism (compiled vs interpreted Python, runtime C, event loop machinery, coroutine completion, context vars, Jinja template code, werkzeug request/response objects), then fix the biggest mechanisms in general form. Candidate tickets: luc-j7zk, luc-s4lm, luc-mltz, luc-6ljp, luc-f7cn.


## Notes

**2026-11-09T07:41:52Z**

Attribution (perf -X perf, master, by mechanism) and what the branch did.

Flask (in process, test client, app mix; master 4.9x CPython 3.14t per request). Flat profile, no item over 8%. Biggest named mechanisms on master:
- Calls of classes and objects with a Python __call__ from C or compiled code went _PyObject_MakeTpCall -> lu_slot_tp_call -> call_special_tuple -> wrapper descriptor -> kernel_wrapper -> nested eval: about 65% of samples had MakeTpCall on the stack, and units were entered 22.7M times per run (6.1M after). DONE: lu_call calls the type's Python or kernel __call__ directly.
- Jinja's Environment.getattr on dicts raises AttributeError per field (100 per index page): the kernel made it with a keyword class call through type.__call__ and a nested eval; 7.6% inclusive. DONE: C primitive _lucifer.attribute_error (3.1% after). Getattr miss caught: 7.8 us -> 1.6 us (CPython 0.48).
- PyErr_SetObject asked builtins.issubclass by interning its name from a C string each time; exception __init__ formatted the class name eagerly. DONE (raise+catch 1.1 us -> 0.68; CPython 0.19).
- ABC checks through _py_abc (WeakSet of weakrefs hashed in Python): werkzeug iter_multi_items 3.3%. DONE: lib/modules/_abc.py with identity-keyed weak caches (docutils and typing_runtime_protocols now pass).
- Context vars: current context in a dict keyed by thread ident (a new thread could inherit a dead thread's context: bug, fixed), ContextVar.get 0.94 us (CPython 0.045). DONE: ts->context; get 0.2 us.
- Remaining: ~95 interpreted generator resumes per request (Jinja yield-from delegation, luc-hhnm), werkzeug URL quoting and regex, per-call cost (luc-callcost), exceptions still 3.5x (luc-msp6).

async_tree (master 7.9x geomean over 16 variants): event loop machinery all in pure-Python asyncio (no _asyncio). CPython pure-Python asyncio is itself 2.7x its C accelerator, and master was ~4.5x CPython-pure. Shares: Task.__init__ 12% (task name f-string via kernel format_value, WeakSet registration), __step_run_and_handle_result 9% (StopIteration per completion), _call_soon 6%, Handle._run 5% (Context.run), _get_running_loop 3% (threading.local __getattribute__ in Python), iscoroutine 2.3% (set of types via kernel), Future.__schedule_callbacks 4% (x[:] = [] in kernel), dict ops on loop/task keys through kernel hash_of, cycle collector ~6%, C-API issubclass interning. DONE: contextvars, tp_call bypass, native int f-strings, native dict/set hashing for identity-keyed objects (cached per type version), set membership native, IsSubclass/IsInstance fast paths, threading.local without lock (small gain: its Python __getattribute__ dominates).

Results (bench host, best of 3, final HEAD): async_tree family 7.94x -> 5.43x CPython (0.68 of master); async_generators 15.6x -> 12.6x; coroutines/generators within noise (generators instructions -2.5%). Flask mix 1757 -> 1429 us (CPython 361); web req/s 1/2/4/8 CPUs master 464/661/1540/2191, branch 564/982/1904/2682 (later runs 916-935 at 2, 1768-1899 at 4, 2507-2657 at 8). pyperformance geomean 4.19 -> 3.73 (70 common), AWFY branch/master 1.003 (instructions equal on Json, DeltaBlue).

Half-done / not done: gates not run (check-harness and abi-audit passed at 9028ea7's code minus the last dict commit; make check, check-jit, check-compiler not run; check-kernel earlier showed only null_item_repr needing _testlimitedcapi). threading.local still slow (needs a native getattro with a per-thread dict). Leftover tickets: luc-nnl3 (deopt stress bug with lowered kernel exception class call), luc-7qkn, luc-y9af, luc-hhnm, luc-msp6.
