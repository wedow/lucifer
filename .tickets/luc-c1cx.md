---
id: luc-c1cx
status: closed
deps: []
links: []
created: 2026-11-04T17:16:05Z
type: feature
priority: 0
assignee: Greg Wedow
parent: luc-d5u1
---
# P0a: JIT spike that validates the bet on four AWFY benchmarks

Validate DESIGN decisions 5, 7 and 9 early with the thinnest real slice of the block-SSA tier (docs/DESIGN.md 6.2), built in its final shape (runtime/jit/, real guards and deopt to the interpreter, not a throwaway), before more interpreter-speed investment.

Scope: back-edge-triggered compile (synchronous is fine for now) of hot functions to block SSA from bytecode plus inline-cache feedback; guards on type version, keys version, function version; kernel inlining of the Python builtins those benchmarks hit (list.append/__getitem__/__setitem__/len, dict.get, range iteration); int and float unboxing with overflow guards; escape analysis and scalar replacement for non-escaping temporaries; refcount elision of balanced pairs; deopt at a bytecode boundary that rebuilds interpreter frames (including inlined kernel frames) and materializes virtual objects once; linear-scan regalloc and an x86-64 encoder. Port algorithms from the donor (the donor VM: docs/vm/optimizing-tier-DESIGN.md, optimizer-backend-DESIGN-a.md and the optimizer_ssa_*.inc sources); C only.

Acceptance: -X lucifer_jit=always and default runs of AWFY Sieve, Bounce, NBody and Richards print results identical to the interpreter and CPython; -X lucifer_stress=deopt:7 also identical; make check and difftest unchanged with the JIT on. Report a table (best of 5, quiet host): CPython 3.14t, lucifer interpreter, lucifer JIT, for each of the four, plus where JIT time goes (perf) and which design assumptions held or failed. The result decides how the remaining P0 is built.


## Notes

**2026-11-04T20:58:09Z**

Spike done (docs/jit-spike.md). AWFY best-of iterations: CPython 3.14t / interp / JIT: Sieve 2.14/5.73/2.16 s, Bounce 1.69/5.15/4.01 s, NBody 1.94/170.9/1.96 s, Richards 4.25/16.9/10.4 s. Held: unboxing + kernel inlining (NBody 87x over the interpreter, CPython parity), exact layouts at boundaries, deopt through inlined kernel frames, in-line guards correct under threads. Not yet: beating CPython; only 15-27% of time is in compiled code, the rest is dict-per-instance attributes, critical-section stores, allocation and non-inlined calls. Next: shared-key inline attributes, inline store/alloc fast paths, lazy frames, direct calls, kernel resolvers. always-lane leftovers: luc-efyg.
