---
id: luc-1yki
status: closed
deps: []
links: []
created: 2026-11-09T09:30:06Z
type: bug
priority: 1
tags: [jit, perf]
---
# jit: unpack_sequence runs interpreted after its hoisted guard fails

Pure-python gap lever 7. do_unpacking is called with a tuple and then a list by turns. The tuple type guard is hoisted out of the loop; when it fails no lasting replacement unit is built, the loop runs in the interpreter, and per-value time degrades from 88 to 120 ns. Its unit is about 1 MB. Fix the missing rebuild, bound unit size adaptively if huge units are the cause, and add a jitcheck growth lane that fails on master.


## Notes

**2026-11-10T02:06:23Z**

Root cause: a replacement unit was built, but late and not for long. The one hoisted tuple guard stood for all 400 sites; its failure marked only one site, so each value killed the 1 MB unit twice (hoisted guard, then the next site's in-place guard), the loop ran interpreted through each wait (WARMUP + code size entries), and the next site failed in turn. UNPACK_SEQUENCE also speculated a tuple whenever the feedback was not lists only. Fixes: a site whose feedback holds both exact tuples and lists takes either through one C helper (unpack_exact), so one recompile settles; the generic helper unpacks exact tuples and lists in C instead of the kernel (polymorphic 10-item unpack, 10 per loop: list 1054 -> 85 ns, tuple 31 -> 18). Huge units were the second cause: compiled straight-line code costs 15 ns per unpacking within the L1, 29 at 260 KB, 49 at 520 KB and 73 at 1 MB, against 33 interpreted; a block larger than the core's L2 (sysconf) now fails the compile and backs off, the crossover point, so no cost cliff. unpack_sequence -n 12 values: master 87-149 ns (114-149 after value 3), branch 87-89 with 119-122 on the backed-off retries; compiles of do_unpacking 8 -> 0 published, code 12.5 MB -> 2.6 MB. Daily -n 3 unchanged (90.2 -> 89.4; 3.14t 55.5). Left: compiled code per unpacking is about 2.5 KB (per-store decref slow paths); shrinking it is what would let this loop compile. tests/kernel/jit_unpack_phases.py, jitcheck lane hoisted (fails on master: 26321 -> 44501).
