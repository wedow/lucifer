---
id: luc-kizs
status: open
deps: []
links: []
created: 2026-11-11T02:06:09Z
type: bug
priority: 1
tags: [jit, flaky]
---
# jit_mega_iter returns a wrong item intermittently

make check-kernel (plain lane) fails tests/kernel/jit_mega_iter.py about 2 runs in 5 on master d7d434b, and the same on luc-asqh (unrelated float parsing change). The first output line differs from the oracle in one item of the megamorphic str.join / set() loop results:

-6600 ['ab', 2, 'ab', 2, 'ggg', 1, 'ab', 2, 'ab', 2, 'ba', 2]
+6600 ['ab', 2, 'ba', 2, 'ggg', 1, 'ab', 2, 'ab', 2, 'ba', 2]

A wrong result, not a timeout: likely the in-line megamorphic iterator step (a3b985e) taking a stale class arm or iterator state under timing-dependent compilation. Repro: tools/kernelcheck --programs jit_mega_iter, repeated.

