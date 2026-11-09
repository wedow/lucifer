---
id: luc-y9af
status: open
deps: []
links: []
created: 2026-11-09T02:01:52Z
type: task
priority: 2
parent: luc-vxsk
tags: [perf, asyncio]
---
# asyncio: task registration and finalizers cost a weakref, a WeakSet hash through a slot wrapper and a nested eval per task

From luc-vxsk, async_tree after the branch: Task.__init__ is 12% inclusive; WeakSet.add (_scheduled_tasks) is 3.6%: weakref creation with a callback, then hash_of(weakref) reaches ReferenceType.__hash__ through lu_wrapper_descr_call/kernel_wrapper and a nested eval loop (luc-o8i6's two C calls). Task.__del__/Future.__del__ run per object through slot_tp_finalize (nested eval), and weakref clearing at dealloc is 2.3%. The cycle collector is about 6% (visit_decref, visit_clear_unreachable). CPython's C _asyncio keeps tasks in a linked list instead. Options: a native weakref hash path in hash_of, finalizers entered without a nested loop, or a native task registry behind asyncio.tasks._register_task.

