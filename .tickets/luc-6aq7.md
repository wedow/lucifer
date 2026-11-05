---
id: luc-6aq7
status: open
deps: []
links: []
created: 2026-11-04T23:38:03Z
type: bug
priority: 1
parent: luc-d5u1
tags: [jit]
---
# check-compiler driver spun at 100% CPU once with the JIT on by default; not reproduced

Seen once (a JIT-default tree): make check-compiler's lucifer driver (-I -S -c DRIVER over a quarter of the lib corpus, gc disabled) spun at 100% CPU, 48 MB RSS, for 36 min; reruns finish in 20-150 s, and another builder saw check-compiler pass its 1 h limit. Not reproduced: about 420 driver runs over the lib corpus (-X lucifer_jit=on and always, at 4 and 12 processes, with the jit-lane fixes and latest master) and 25 make check-compiler runs all finished in 13-38 s per chunk. A hung driver should be profiled before it is killed (perf record -p PID; then SIGABRT with -X faulthandler for the Python stack).

## Acceptance Criteria

The hang is reproduced and its cause fixed with a test, or the tree it came from is shown to carry a non-JIT cause.

