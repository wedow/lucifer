---
id: luc-349f
status: open
deps: []
links: []
created: 2026-11-19T07:47:36Z
type: bug
priority: 2
---
# threads resizing an array through its methods crash the interpreter (a frame resumed with a NULL on its stack)

Six threads on one array('d') (two read a[i], one stores a[i] = v, two loop extend/append/del a[3:-3]/pop, one takes and releases memoryview(a)) crash with SIGSEGV in about 1 run in 25 with default flags on master 7937512e (1 of 30 runs), and in about 1 in 10 with the element intrinsics. Core stacks: an interpreted frame reached from compiled code (lu_jit_enter -> call_generic -> vectorcall_frame -> lu_eval) whose stack holds NULL where an object belongs: array._pack's generator expression resumed at its outer CALL with the inner call's result missing; Struct._pack in init_new_instance(zip, obj NULL); resizer bodies unwinding with no exception set (lu_traceback_here, exc NULL). The resizing methods are array.py Python (_pack's generator, struct.Struct._pack); single-threaded runs of the same operations are clean. Repro: tests/kernel/threads_typed_items.py with its resizer calling a.extend([item] * (r % 70)), a.append(item), del a[3:len(a) - 3] and a.pop() instead of bytearray's methods; run it 30 times and count exit 139. Same family as luc-t6j2 and luc-4dnw (frames and units under threads).

