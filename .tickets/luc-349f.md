---
id: luc-349f
status: closed
deps: []
links: []
created: 2026-11-19T07:47:36Z
type: bug
priority: 2
---
# threads resizing an array through its methods crash the interpreter (a frame resumed with a NULL on its stack)

Six threads on one array('d') (two read a[i], one stores a[i] = v, two loop extend/append/del a[3:-3]/pop, one takes and releases memoryview(a)) crash with SIGSEGV in about 1 run in 25 with default flags on master 7937512e (1 of 30 runs), and in about 1 in 10 with the element intrinsics. Core stacks: an interpreted frame reached from compiled code (lu_jit_enter -> call_generic -> vectorcall_frame -> lu_eval) whose stack holds NULL where an object belongs: array._pack's generator expression resumed at its outer CALL with the inner call's result missing; Struct._pack in init_new_instance(zip, obj NULL); resizer bodies unwinding with no exception set (lu_traceback_here, exc NULL). The resizing methods are array.py Python (_pack's generator, struct.Struct._pack); single-threaded runs of the same operations are clean. Repro: tests/kernel/threads_typed_items.py with its resizer calling a.extend([item] * (r % 70)), a.append(item), del a[3:len(a) - 3] and a.pop() instead of bytearray's methods; run it 30 times and count exit 139. Same family as luc-t6j2 and luc-4dnw (frames and units under threads).


## Root cause

Not the array code: a compiled root's RETURN_VALUE pops its value, unlinks the frame and then releases its locals, and each release that may run a finalizer published the state after the instruction with an exit (emit.c settle: the frame made real by a walker, or the unit dead). That state names RETURN_VALUE with the value popped and the locals cleared, so an exit from it resumed the return on an empty stack and returned whatever lay below, NULL. Another thread's write killing the unit while an activation ran it made the first noisy release of the return leave so. Cores on master 1929ea93: float.__new__ (the kernel's, behind float(value) in _struct._float_bytes) returned NULL into init_new_instance; LUCIFER_JIT_DEBUG before another showed "exit frame Struct.pack@17 op 35" and then array.extend returning an error with no exception; the units involved were dead and their exits at RETURN_VALUE had nstack 0 and every local NULL.

## Fix

A returning root's releases take no state (eval_emit.c): the value is the caller's, the frame is off the chain for walkers, and the return relies on nothing the unit's death takes back. Regression: tests/kernel/jit_return_release_kill.py (a finalizer run by the return's release rebinds a global the unit relies on; master crashes on it with default flags).

Runs exiting 139, master / fixed: this ticket's repro (threads_typed_items with the array methods) 2/30 / 0/30; jit_return_release_kill.py 1/1 / 0/1 default, passes jit and jit-deopt on both.
