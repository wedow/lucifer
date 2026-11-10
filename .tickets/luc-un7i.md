---
id: luc-un7i
status: open
deps: []
links: []
created: 2026-11-09T00:16:09Z
type: bug
priority: 2
tags: [jit, threads]
---
# threads_set_race hangs now and then under -X lucifer_jit=always

tests/kernel/threads_set_race.py times out in the kernelcheck jit lane on master too: 2 of 8 runs of master 16a62be (found by luc-hez4's make check, where it failed once; the branch passed 8 of 8 reruns). A hang, not wrong output: the oracle's four lines never print. Find which set operation a thread waits in.


## Notes

**2026-11-10T16:43:49Z**

Not luc-chbr's signal race. A hung run (master, -X lucifer_jit=always, run 59 of 100): one thread spins in lu_set_insert_clean under lu_set_resize (prim_set_resize) holding the set's critical section, a second waits for that mutex in lu_set_add_native, main waits in join. The set has used=160 while resize was asked for minused=120, so the new 128-entry table fills and insert_clean probes for ever. lib/kernel/set.py computes minused from size(so) before prim_set_resize takes the critical section, and other threads add meanwhile. Likely fix: lu_set_resize sizes from max(minused, so->used) under the lock.
