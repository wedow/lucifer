---
id: luc-kizs
status: closed
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


## Notes

**2026-11-11T03:37:32Z**

Root cause: the test, not the tier. kernelcheck runs each program as INTERPRETER -I -S, and -I ignores PYTHONHASHSEED, so the oracle and lucifer each hash with a random seed and a two-str set's order differs between the two processes about half the time (python3.14t -I -S -c "print(''.join({'a','b'}))" prints ab and ba in turn). The failing item is warm()'s ''.join of the set literal {'a', 'b'}, which 67a0272 (on master since 66c654b) sorts before printing; this ticket was filed against d7d434b, before that. On e2acbf0, kernelcheck --programs jit_mega_iter passes 5 of 5 runs in the plain lane and 5 of 5 in the jit lane, with the host fully loaded.
