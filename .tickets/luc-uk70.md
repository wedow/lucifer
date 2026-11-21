---
id: luc-uk70
status: closed
deps: []
links: []
created: 2026-11-21T05:57:10Z
type: bug
priority: 2
tags: [jit, determinism]
---
# jit: build output depends on the compiler's own memory layout

Found attributing luc-7vga. Two lucifer binaries whose only difference is an unused int field added to struct ctx (runtime/jit/eval.h) build different code from the same program under PYTHONHASHSEED=0: in pyperformance json_loads (-l 20, LUCIFER_JIT_DEBUG=1) Random._store@18 compiles to 10458 bytes on master and 10921 bytes with the field, with the same 24 calls inlined; the runs then diverge (403 vs 408 units, 9.10 vs 9.44 MB emitted, 14459 vs 16541 exits). Each binary is deterministic run to run. A third binary that also changes eval_call.c moves int.to_bytes@1 from 15611 to 15802 bytes the same way. Whole-program effect: json_loads +0.63% instructions (20.295G vs 20.423G, core lease, same checkout path), deepcopy about +1%. Something in the build reads memory whose contents or addresses follow the arena or struct layout (an uninitialized read, or pointer order/hash deciding a choice). Repro: build master, add 'int spent;' after 'int depth;' in struct ctx, build again, diff the 'jit: compiled' lines of the two runs above.


## Notes

**2026-11-21T06:37:08Z**

Not a compiler layout dependence: the variation is entropy. The default random instance (random._inst = Random()) seeds from getrandom at import, and Random._store@18 compiles during that seeding with values that differ per run. Repro driver: bm_json_loads's bench_json_loads x20, PYTHONHASHSEED=0, LUCIFER_JIT_DEBUG=1, 'jit: compiled/failed' lines hashed. Unpinned: master alone gives 3-4 distinct compile sets in 6 runs (Random._store@18 10921 vs 10458 bytes, getattr_hook@1 25336 vs 20968), with or without ASLR, and the +field binary gives the same sets. With getrandom pinned (LD_PRELOAD shim filling the buffer with a constant): master 4/4 and master+unused int field in struct ctx 4/4 give one identical compile set, ASLR on and off. So the luc-7vga A/B (+0.63% json_loads) was within seeding noise; instruction-count A/Bs of programs that import random need getrandom pinned.
