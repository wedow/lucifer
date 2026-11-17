---
id: luc-eih1
status: closed
deps: []
links: []
created: 2026-11-17T07:54:44Z
type: bug
priority: 2
---
# capi: dict_watch_resurrect_run differs under -X lucifer_jit=always

On master (55b5f8c and 101cb05) tests/capi/dict_watch_resurrect_run.py prints the oracle's output with default flags but not with -X lucifer_jit=always: the watcher sees different values ('a', 2)/('a', 3) where the oracle has ('a', 1)/('a', 0). make check-capi LUCIFER_FLAGS='-X lucifer_jit=always' fails on it.


## Notes

**2026-11-17T14:34:52Z**

Root cause (11-17 floor triage): a compiled unit holds a strong reference to each global it burned in as a constant (lu_ev_dict_const, runtime/jit/eval_attr.c). When the global is rebound the unit dies, but dead units are freed (constants released) only at the next collection's reclaim (lu_jit_reclaim). Until then the old value stays alive, so finalizers and dict watcher DEALLOCATED events fire late. Pure-Python repro, no C-API needed:

    class Foo:
        def __init__(self, n): self.n = n
        def __del__(self): print('del', self.n)
    def h(): return r is None
    for i in range(4):
        r = Foo(i); h(); print('rebind', i)

CPython prints 'del i' right after each rebind; -X lucifer_jit=always prints 'del 0' only at exit (or at the next gc.collect()). Not cheap: releasing a dead unit's constants at kill time is unsafe while an activation may still use them (borrowed in registers); options are a reclaim requested at the next poll (stop-the-world cost per death), or holding a global's value through the guarded dict slot instead of a unit-owned reference. Also: make check-capi no longer stops at this failure (the 18 capi MISSING rows in the deopt floor were the tests after it).

**2026-11-17T16:10:35Z**

Fixed (commit 'fix: free a rebound global's old value at the rebind under the JIT'). A dict change that kills a unit marks it stale and a release due; replace/remove/popitem/clear of a watched dict then run a reclaim with the world stopped (lu_jit_release), freeing dead units no activation is in, so the old value dies at the rebind; a stale unit an activation is in goes when that activation deopts back to the interpreter. Sites already invalidated (NOHOIST) load non-code globals and module attributes through the cache instead of a guarded constant, which held the old value while the unit lived. tests/kernel/global_rebind_frees.py fails on master in jit and deopt, passes now; check-capi jit=always PASS incl. dict_watch_resurrect_run; floor quick PASS off/jit/deopt (561 s). The two libtest cases are not this bug: they fail with the JIT off too (luc-4uge). Residual: a function/class/module value at a NOHOIST site stays a guarded constant, so a rebound function's old object can still live until its unit dies.
