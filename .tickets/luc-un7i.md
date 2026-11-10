---
id: luc-un7i
status: closed
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

**2026-11-10T17:44:14Z**

Fixed: every set table size is now chosen under the set's critical section. lu_set_resize became static; lu_set_reserve (room for n more keys) and lu_set_rebuild (sized for the keys held) read fill, used and mask under the lock. Prims set_reserve and set_compact replace set_resize, so set.py's merge, update_dict and difference_update no longer pass a size read before the lock. set_merge_clean reserves room for the other set's keys first, because a clear between the caller's presize and the merge leaves the 8-entry small table. Dict and list were checked: their resizes (insertion_resize, list_resize) take sizes from the object under the lock, and list_splice rejects a stale length. Test: tests/kernel/threads_set_resize.py (update from a set and from a dict racing adds, update racing clear, difference_update racing adds). Under -X lucifer_handoff it hung 11 of 20 runs before and 0 of 20 after. threads_set_race under -X lucifer_jit=always: 200/200.
