---
id: luc-yf1i
status: open
deps: []
links: []
created: 2026-11-22T04:04:34Z
type: task
priority: 1
assignee: Greg Wedow
tags: [perf]
---
# binding a callee frame dominates compiled-to-compiled call cost

luc-2vj6 follow-up (read its notes): direct unit-to-unit calls fired (mdp lu_eval entries 36.2M -> 1.0M) but saved nothing, because skipping lu_eval is cheap next to binding the frame. Profile frame binding on mdp, async_tree and deltablue (argument copy, defaults, kwargs matching, cell/free setup, frame allocation and zeroing) and make the common case (positional args, no cells, exact count) a straight copy with nothing generic. Also check whether CALL_FUNCTION_EX now binds generically where lu_call used to reach the emitted direct entry (deltablue +83M instructions on the 2vj6 branch).


## Coordinator 11-22: not landed, branch luc-yf1i kept
Null on real rows: mdp +0.01%, deltablue +0.06%, async_tree -0.37% (instructions); only the JIT-off positional microbenchmark moved (-2.6%). Frame binding is not where the direct-call saving went; reopen with a profile of what the 2vj6 direct entry actually spends per call before another attempt.
