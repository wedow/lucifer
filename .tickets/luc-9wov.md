---
id: luc-9wov
status: open
deps: []
links: []
created: 2026-11-07T17:52:52Z
type: bug
priority: 2
tags: [jit, perf]
---
# jit: an inlined callee's loop keeps its ints boxed

Found by luc-keyu: with Sieve._sieve inlined into Sieve.benchmark (through callee feedback on self._sieve, a staticmethod), the inlined loops box and unbox their loop-carried ints every iteration (17.4K boxes per entry against 669 in _sieve's own unit; 2.8x the instructions). luc-keyu therefore does not inline a callee that loops when only feedback names it. Find why unbox_phis or the escape rule keeps an inlined frame's loop phis boxed, then lift that restriction (made_function and call_dispatch's loops()).


## Notes

**2026-11-08T10:03:58Z**

luc-esc2: one cause fixed (538dd59): unbox_phis took a NULL input (a local unbound on that path) only in a root local's phi, so Sieve's k in the inlined _sieve stayed boxed with a box at each header; constant-start phis (s = 0) are raw too (6a3d1f7). With loops() lifted, Sieve went from 2.9x to 1.13x _sieve's own unit's instructions (70.5G vs 62.5G for 20 x 3000; cycles 57.0G vs 52.4G), so the restriction stays until the rest is found.
