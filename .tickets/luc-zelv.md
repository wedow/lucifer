---
id: luc-zelv
status: open
deps: []
links: []
created: 2026-11-09T08:58:06Z
type: epic
priority: 1
---
# Kernel customization: per-site specialized kernel code, operators as sends, fused iteration

Kernel builtins and operators are generic Python dispatchers shared by every call site; their feedback is merged and many opcodes cannot appear in inlined frames, so ordinary idioms run 2.5-15x slower than CPython 3.14t. Build the Smalltalk-style properties: customization (kernel code specialized per call site from that site's feedback), operators as sends with class feedback, iteration builtins fused into the consuming loop, and a kernel library written for the compiler. Design: docs/design/kernel-customization.md.


## Notes

**2026-11-10T10:36:21Z**

Branch luc-custom. Landed: design (docs/design/kernel-customization.md); stage 1 operators as sends (class feedback per operator site, folded kernel binary_op/compare/unary per class pair, INT|FLOAT arms, float pow lowering, context-keyed exit marks); stage 2 Python-running helpers in inlined frames at a call's cost, kept-frames fix; stages 3-4 kernel twins (min/max/sum/str/startswith/endswith/bytearray.extend), builtin calls customized by argument class, type(x) is T facts, never-rebound params keep facts in loops, FOR_ITER steps of other iterator classes in line (zip/enumerate/dict views) with MISSING routed to the loop exit. Results vs master cd44643 on the bench host: 15-program geomean 3.96 -> 2.66 of CPython 3.14t (raytrace 6.6 -> 1.50, spectral_norm 4.6 -> 1.14, chaos 1.49 -> 0.52, nbody 0.93 -> 0.57, telco 68 -> 52, unpickle 17.5 -> 13.8); pyperformance 3.65 -> 3.42; AWFY 0.379 -> 0.375; cli 6.74 -> 7.19 (regression, luc-5rl1); Flask in-process mix 1402 -> 1354 us (CPython 365). Micro-kernels: 38 of 72 faster than CPython 3.14t, 39 faster than master by >13%; regressions repr(float) (luc-obz9), bytearray.extend (luc-oke3). All five gates 0 at 1a04f48. Not done: iteration fusion for generator-expression consumers (luc-g0um), container intrinsics stage 5 (luc-wov9), adaptive budget after folding (luc-272e), call-arms defect on type() (luc-wswp), zip/enumerate step overheads (luc-65km).
