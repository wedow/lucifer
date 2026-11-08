---
id: luc-sl4y
status: closed
deps: []
links: []
created: 2026-11-05T14:40:17Z
type: bug
priority: 2
assignee: Greg Wedow
---
# jit: a call loop runs ~4x slower after a different loop warmed the same callee

Found by the luc-34ha microbench (also on master): with the JIT on, a loop calling local aliases add(s, sqrt(i)) of operator.add/math.sqrt takes ~0.8 s alone but ~3.5 s when it runs after a loop calling the same functions through a class attribute (class K: add = operator.add; K.add(s, i)) in the same process. Order-dependent JIT state: likely feedback or retirement (luc-jh5s rules) from the first loop pinning the callee in a slow shape. Reduce to a kernel program, find the mechanism, and add a jitcheck lane that runs the two orders and fails on a big ratio.


## Notes

**2026-11-07T16:31:41Z**

Fixed on luc-keyu: the alias call site now inlines the builtin by callee feedback with its own operand types; tests/kernel/jit_call_order.py and the jitcheck order lane run both orders (loop b 0.07 s in either order, was 0.69 s alone and 2.2 s after loop a).
