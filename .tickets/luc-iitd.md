---
id: luc-iitd
status: open
deps: []
links: []
created: 2026-11-25T15:55:30Z
type: bug
priority: 0
---
# jit: rebuilt inlined frames take their code from the function

When compiled code deopts with inlined frames, push_inlined (runtime/jit/exit.c) rebuilds each one with lu_frame_push(func), which takes the code from func->func_code. A __code__ assignment while those frames run (it kills the unit through its function dependency, so it deopts) leaves the function naming the new code: the rebuilt frame then carries the new code with an instruction of the old one (tracebacks name the new code's function with line None: tests/kernel/jit_direct_calls rebound case), and it is sized from the new code's co_framesize while the old code's locals are written into it: an overrun when the old code's frame is bigger.

Exposed on master by 2ec83fb3 (code with monitoring data compiles again once its tools have gone), which let the rebound case run compiled; 94e59f99 passes.

Fix: one frame-push path that takes the code explicitly (lu_frame_push its thin caller), used by push_inlined with the record's code; audit every other place that rebuilds or sizes a running frame from the function.


## Notes

**2026-11-25 (fix).** Every place that builds or sizes a frame for a function, after its __code__ may have been replaced:

- runtime/jit/exit.c:631 push_inlined (a deopt's inlined frames): wrong, took func->func_code. Now lu_frame_push_code with the record's code (fr->code).
- runtime/jit/exit.c:241 and runtime/jit/helpers.c:289 (a virtual generator made real at an exit or by h_gen_new): wrong, lu_gen_blank sized and named the frame from func->func_code and filled it with the compiled code's locals and stack. The generator's record now carries its code (GEN_CODE, ssa.h), passed to lu_gen_blank; the evaluator reads a generator's code from there too (eval_gen.c), not through its function.
- runtime/jit/helpers.c:938 call_function (a call compiled code makes through C): racy, the arity was checked on one read of func_code and the frame pushed from a second; a replacement between them on another thread could size the frame from a code with fewer parameters. Now one read, the frame pushed with it.
- runtime/jit/exit.c:509 record (a walker's record of an inlined frame) and :475 gen_frame (an existing generator's frame): right, sized and named from fr->code or the generator's own frame.
- The root frame's deopt and runtime/jit/exit.c:427 lu_jit_sync: right, they write into a frame that exists with its own code.
- runtime/jit/jit.c:1769 lu_jit_direct and the direct-entry stub (runtime/jit/x86_64/emit.c:5440): right, the stub pushes the frame for its unit's own code, chosen by one read of func_code: a replacement after that read is a call that began before it.
- runtime/interp/ceval.c:1889 lu_frame_push_exact, lu_frame_call (call.c, binds against f->code), lu_frame_push's other callers (ceval.c, builtins.c, type.c): right, a new call reading the code once.
- runtime/interp/frame.c:519 f_code: right, it returns the frame's code.
