---
id: luc-6b0l
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# threads: serialized mode; no object locks while one thread runs Python at a time

Measured: the lock byte taken and released around every compiled attribute store costs about 32 cycles; stubbing the two emitters makes store-heavy loops 1.7-2.3x faster and a Bounce-shaped loop 1.66x. PyCriticalSection_Begin/End are 2.3% of real-program steady state and 45% of a list comprehension.

Design. The runtime is in one of two states, serial or parallel.
- Serial: at most one task runs Python at a time. Switches happen only where compiled code polls and where C blocks or detaches. No store sequence contains a poll, so compiled code needs no lock byte, no atomics on reference counts it owns, and C critical sections need not lock (a section is already released wherever the task could be switched out). An extension that needs the GIL is safe.
- Parallel: today's protocol.
- Compiled code that leaves out locks records a dependency on the serial state. Entering parallel stops the world and invalidates those units, as any other broken assumption does.
- The state follows the program, never a flag or environment variable. Stages:
  1. Serial while only one task has ever run; the second task's start enters parallel. Lock-free INLINE_STORE/_I/_F, LIST_STORE and the other lock_object sites; critical sections skip the lock in serial.
  2. A run token so several tasks can stay serial; Py_mod_gil honoured (md_gil is recorded and ignored today, sys._is_gil_enabled() answers False): importing a module that needs the GIL enters serial, as CPython re-enables the GIL.
  3. Adaptive: stay serial until tasks wait runnable for the token for a sustained share of time, then parallel; back when the contention is gone.
- Later (own ticket when the idiom table shows it): objects confined to one thread keep the lock-free path in parallel, published by a store barrier.

Acceptance, stage 1: idiom rows store-int-attributes <= 65 cycles, store-object-attribute <= 35, store-float-attribute <= 42 with instructions down; a threaded differential test that starts its second thread mid-loop over shared objects stays correct in every JIT mode; the free-threading corpus and the thread kernel tests unchanged. Full review (locks, races).


## Notes

**2026-11-22T23:31:05Z**

Stage 1 built on branch luc-6b0l (not landed). Serial until a second task is made; compiled INLINE_STORE/_I/_F, LIST_STORE and TYPED_LOAD/STORE built while serial take no lock and depend on the serial state (LU_DEP_SERIAL); critical sections begun while serial are pushed suspended. Idiom cycles/iteration, base -> branch (min of 3, interleaved, one core): stores 120 -> 50, pstores 65 -> 33, fstores 63 -> 29, oo 181 -> 107, listcomp 122 -> 91, listidx 121 -> 115; loads 65 -> 64; calls 37 -> 36..48 on identical code (noise). Blocker: the switch stops the world at the second task's creation and so waits for the first task to poll or detach. An attached thread spinning in C while a raw pthread calls PyGILState_Ensure (tests/capi/poll.c, tests/capi/mutex.c) now deadlocks; free-threaded CPython allows that. Needs a handshake that does not depend on the first task polling before this can land.

**2026-11-23T00:08:55Z**

Stage 1 reworked (branch luc-6b0l): the switch no longer stops the world. The thread making the second task sets parallel, kills the serial units, issues membarrier(PRIVATE_EXPEDITED) (registered at startup and again in a fork's child; without it the runtime starts parallel), then waits until the first task is detached or shows both of its words at 0: unlocked_jit (0 only inside a published call of a unit built serial) and unlocked_c (C critical sections begun without a lock, and the trylock-skip stretches in store_at and lu_member_exchange, bracketed: three plain instructions against two lock cmpxchg). tests/capi/poll.c and mutex.c pass again. Accepted: a first task that sits forever in foreign C, or in a non-publishing instruction's callee, while it holds a section begun without its lock makes the switcher wait (free-threaded CPython would let the second thread go on and block on that object's lock). Idiom cycles/iteration, base -> branch, two rounds of min-of-3 interleaved: stores 119/123 -> 63/60, pstores 73/67 -> 42/45, fstores 67/66 -> 22/37, oo 186/175 -> 112/112, listcomp 109/116 -> 87/89, alloc_escape 202/209 -> 198/200. pstores misses <= 35: in one lease the build with every lock stubbed gives 35/39 and this one 42/41; the rest is the unit-death load in the frame check after each store, which out-of-line slow paths (luc-bpmh) would move off the fast path. For the borrowed-reference work: LIST_ITEM spends about 6 instructions on checks only other threads need (the array NULL, the capacity, the item NULL); the owner-thread compare in reference counts does not drop simply while serial, since the handoff scheduler moves one task across carriers with different thread ids.

**2026-11-23T01:22:50Z**

Review fixes (rebased on origin/master dd9ce457). The state goes serial -> switching -> parallel; the switcher sets parallel last, after the kill, the barrier and the wait, and every task creation waits for parallel, so a third task no longer runs beside a first task still unlocked (tests/capi/serial_third.c). A first task that meets a switch locks its own sections and waits. The switch lock is in the fork registry: no fork lands inside a switch. A barrier refused after registration falls back to stopping the world (tests/capi/serial_sandbox.c, seccomp). A detach makes sections begun without their lock ordinary suspended ones, so the switch waits only on sections relied on since the last detach. Compiled code keeps unlocked_jit at 0 while C it calls may run Python without published state. Accepted wait, now only: the first task sits attached in foreign C that never returns (or in a callee of an instruction the helper table says runs no Python) while it relies on a section begun without its lock since its last detach. Idioms, new base -> branch, two rounds: stores 114/118 -> 32/34, pstores 62/62 -> 24/24, fstores 66/68 -> 23/25, oo 177/176 -> 77/74, listcomp 99/111 -> 81/82, alloc_escape 188/190 -> 173/176.

**2026-11-23T02:04:58Z**

Round 2 fixes: whoever takes the switch lock (a task maker, a fork) waits for it detached and blocks instead of spinning, so a switch stopping the world for want of the barrier counts that task as stopped (tests/capi/serial_sandbox_new.c); a fork takes the switch lock before the stop-the-world mutex, one order for both paths (tests/capi/serial_sandbox_fork.c). Accepted waits, all of them: (1) with the barrier, a first task that sits attached forever in foreign C, or in a callee of an instruction the helper table says runs no Python, while it relies on a section begun without its lock since its last detach; (2) only when the barrier is refused after startup (a sandbox's seccomp filter), the stop-the-world fallback also waits for a first task that sits attached in C without polling while another thread attaches, the tests/capi/poll.c pattern, as stage 1 did everywhere.
