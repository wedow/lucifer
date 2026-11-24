---
id: luc-oqb7
status: open
deps: []
links: []
created: 2026-11-24T22:14:49Z
type: bug
priority: 3
assignee: Greg Wedow
parent: luc-orn7
---
# release order differs from CPython's outside the normal return

A returning frame now releases its values from the top of the stack down to the first local, as CPython does (luc-2r1l). The review of that change found four places where the order still differs; each is visible only to programs whose finalizers have side effects. (a) Compiled code: lu_ev_settle (runtime/jit/eval_emit.c) appends the items of a dead container the tier never built (escape analysis) to the END of the pending releases, so in  compiled code releases 'a' before 'b' where CPython gives 'b' then 'a'; the same across inlined frames in throw_to and raised_out (an inner frame's container items after the outer frame's values). Release a dead container's items where the container sits in the order. (b) During exception unwinding the value-stack entries are released after the frame is unlinked and after the PY_UNWIND event (runtime/interp/ceval.c unwinding; the tier's raised_out); CPython pops the stack while the frame is still current and before the event:  with D.__del__ printing sys._getframe(1).f_code.co_name prints g on CPython. (c) frame_dealloc (runtime/interp/frame.c) releases the fast locals first, then f_locals, the function and the code, with f_back last; CPython releases the code, the function and f_locals first, then the fast locals from the top down, then f_back. (d) The emitted return of a directly called unit (direct_done in runtime/jit/x86_64/emit.c) gives the recursion depth back after the unit released its locals; the interpreter and CPython give it back before, so a finalizer of the deepest frame's local can meet RecursionError in compiled code only. Not determined: an inlined callee whose frames a walker turned into real records, returning inside compiled code (its finalizers may walk from the stale record). Each needs a kernel test compared with the oracle in jit-off, plain, jit and jit-deopt.

