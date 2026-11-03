---
id: luc-1dl8
status: open
deps: []
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1a: hello from a host-compiled pyc: marshal reader, heap-frame eval loop, frozen kernel boot, check-kernel runner

First light; settles the native bootstrap order (luc-vvqv, Bootstrap order). Native: the marshal reader (whole r_object of Python/marshal.c v5, incl. code, frozenset, slice, refs; also backs PyMarshal_ReadObjectFromString), layout constructors and hashes (SipHash-1-3 with _Py_HashSecret from PYTHONHASHSEED or getrandom before the first hash; int mod 2^61-1; xxHash tuple) for every marshal type, the namespace dict (exact-str keys) over the keys-table format in runtime/object/dict.h, module/function/cell/PyCFunction layouts, the _lucifer module (multi-phase, module state), builtins/sys module objects and sys.modules. The eval loop: one activation per task, computed-goto ceval.c, frames on the task's chunked datastack with owner tag, instr, lazy frame_obj; every frame entry through lu_enter_frame checking interp->eval_frame; RESUME compares the code's monitoring version; opcode table reserves 234+; poll site at calls and JUMP_BACKWARD reading the carrier's poll word. Opcodes: RESUME, NOP, CACHE skipping, LOAD_CONST/LOAD_SMALL_INT, LOAD_NAME/STORE_NAME, LOAD_GLOBAL, LOAD_FAST*/STORE_FAST*, PUSH_NULL, CALL (Python and PyCFunction), POP_TOP, RETURN_VALUE. Task record = PyThreadState + tail (pin, pending events, datastack); carrier record gains poll word and current task (runtime/carrier.c moves to runtime/thread/). Py_BytesMain minimal: -I, -S, path.pyc. tools/freeze compiles lib/kernel with HOST_PYTHON (magic 3627) into build/frozen.c, no str hashes. Kernel: lib/kernel/builtins.py print(*args, sep, end) over a _lucifer.write(fd, str) primitive. Boot failure before the kernel is up prints the exception type and message and exits 1. tools/kernelcheck + make check-kernel: host-compiles tests/kernel/*.py to build/tests/kernel, runs ORACLE_T live and bin/lucifer under a 512 KiB stack, PROGRAMS= selects, LANE= reserved for handoff.
Implements: DESIGN 3.1, 3.3, 4.1, 4.4 (bootstrap), 5.2, 5.3 (records only), 6.1; FOUNDATIONS 2.2 (record), 2.3 (shape), 2.5 (word), 2.16, 2.18.
Files: runtime/interp/{ceval.c,frame.c,frame.h,code.c,marshal.c,frozen.c}, runtime/object/{dict.h,dict.c,layouts.c}, runtime/prim/{module.c,object.c,str.c,os.c}, runtime/thread/{task.c,carrier.c}, lib/kernel/builtins.py, tools/freeze, tools/kernelcheck, Makefile (check-kernel, frozen.c rule).
Adds tests/kernel: hello.py.

Builder: opus.

## Acceptance Criteria

make check-kernel PROGRAMS="hello" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t); make check-globals and check-capi still pass.

