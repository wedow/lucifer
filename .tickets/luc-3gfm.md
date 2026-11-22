---
id: luc-3gfm
status: open
deps: []
links: []
created: 2026-11-04T13:19:51Z
type: bug
priority: 2
parent: luc-h4gc
---
# LOAD_FAST_BORROW takes a strong reference: Py_REFCNT of a local passed to a call is one too high

CPython 3.14 lends a fast local to the call that uses it, so d.resize(50) sees Py_REFCNT(d) == 1. Extensions inline that test (PyUnstable_Object_IsUniquelyReferenced is a static inline in the cp314t headers), so no runtime hook can answer it: NumPy's ndarray.resize refuses with 'may be referenced by another object' (10 numpytest rows: TestResize, TestSizeOf resize). Needs borrowed stack references in the interpreter, or a deferred-count design that keeps a loaned local invisible.


## Notes

**2026-11-06T01:34:14Z**

From luc-dawf: test_capi.test_object CAPITest.test_is_unique_temporary asserts sys.getrefcount(x) == 1 for a parameter (gh-130704); lucifer gives 2. PyUnstable_Object_IsUniqueReferencedTemporary itself now scans the calling frame's value stack.

## Review 11-21: blocked, branch luc-3gfm kept
Refcount balance is right, but lu_frame_has_borrows refuses compiled entry for any frame holding a borrowed slot (jit.c lu_jit_enter/lu_jit_run), and lu_frame_call keeps borrowed args borrowed, so any f(x) with x a caller local never runs compiled; h_call/h_call_kw exit on borrowed args every call. Measured: call loop 0.037 s -> 1.717 s (46x), JIT entries 4732 -> 27. Rework: the JIT must accept borrowed slots (take ownership at entry or track them), then a perf A/B.

## Review 11-22: blocked, branches luc-3gfm and luc-3gfm-r kept
The rework (head 48d83f7a, rebased on 768282dc's successor) crashes compiled code. Deterministic, 3 of 3 runs each, base passes:
- `tests/kernel/jit_carried_objects.py` in the default lane: SIGSEGV before any output.
- `tests/kernel/jit_gen_first_turn.py` with `-X lucifer_jit=always -X lucifer_stress=deopt:7`: SIGSEGV (a for loop over a generator run in line, peeled on its first turn; the area the rework changed in adopt_at and the MISFIT_LOAN peel).
- jitcheck: `quiet` output differs from CPython; `cmethods` (33,896 -> 53,896) and `perfcalls` (27,377 -> 44,044) grow with the iterations; base has 0 failing. `shapes` helper calls rise 169,525 -> 269,786.

Interpreter-side review found no imbalance: POP() owns, every raw stack read goes through PEEK or lu_stackref_object, unwind and frame teardown close tags, frames escaping (frame objects at retire, generators at creation) take ownership, gen_traverse skips loans, the frame-locals proxy keeps replaced values alive. Adversarial programs (frame.clear and f_locals writes and deletes on the lender, unboxed int and float parameters through deopt, closures, tracebacks, saved frames, super(), generators, comprehensions, kwargs, __init__/__call__, deep raises, settrace writes, 4 threads with a sys._current_frames reader) match CPython in plain, jit, jit-deopt 7 and 3, gc:997 and handoff.

Performance, same checkout path, one core, PYTHONHASHSEED=0. pyperformance whole run n=4 at loops/4, minimum of 5: richards cycles 2.950G -> 2.950G (+0.0%), instructions 4.391G -> 4.507G (+2.6%); raytrace 7.852G -> 7.818G (-0.4%), 12.601G -> 12.715G (+0.9%); chaos 3.822G -> 3.788G (-0.9%), 5.972G -> 6.083G (+1.9%). The 11-21 repro (call loop f(x), and a hot loop in a function called with a local): calls 0.017 s on both, hot loop 0.010 s -> 0.014 s, whole-program cycles about +10%, compiles 14 -> 20, recompiles 6 -> 11, guard exits 65 -> 107, entries 3,749 on both. Eight argument-ownership layouts of one 3-argument function: cycles +5%, instructions +5.8%, compiles 16 -> 28, entries 14,151 -> 10,471 (units are keyed by borrow layout, at most 4 per entry pc; a 5th layout runs interpreted).

Rework: fix the two crashes and the jitcheck regressions first. Then consider binding a loan as owned (one incref) when no unit matches its layout, instead of declining or compiling another variant.
