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

