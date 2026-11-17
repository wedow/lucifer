---
id: luc-nvdz
status: open
deps: []
links: []
created: 2026-11-17T20:46:20Z
type: bug
priority: 2
---
# a shift past 62 bits in an inlined copy keeps leaving: array('q') packing units retire in a loop

_struct._int computes (1 << bits - 1) with bits 64 (codes q, Q, n, N, P). Compiled code emits SHL_I with an overflow guard; the result needs a big int, so the guard fails on every entry (_int@66 op 44). The exit counts against the site and marks it NOSPEC, but every rebuild keeps SHL_I, so a loopless root that inlined _int (Struct.pack, array._pack's genexpr) leaves on every entry, retires after 50, and is rebuilt with the same guard at a higher wait level: array('q', range(n)) under -X lucifer_jit=always prints the retire loop with LUCIFER_JIT_DEBUG. Base and the luc-6exm build both show it; taking the generator's per-turn call in line (luc-6exm) puts _int in one more loopless root that retires so. NOSPEC at a shift site should build the generic shift (or one that boxes the overflow) instead of the guard.

