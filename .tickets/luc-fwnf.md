---
id: luc-fwnf
status: open
deps: []
links: []
created: 2026-11-07T10:15:38Z
type: bug
priority: 1
tags: [jit, perf, scaling]
---
# jit: dependency writers serialize on one global mutex (watched-dict stores, type changes)

luc-x8q2's lu_jit_invalidate (runtime/jit/deps.c) takes one process-wide PyMutex and appends to one change log on every type modification and on every store to a dict compiled code watches (module globals, builtins), whether or not any unit depends on the key. Concurrent writers from many threads serialize on it, a scaling defect.

Measured (N threads each run `for _ in range(200000): counter += 1` on a module global, or `cls.x += 1` on a class of its own; best of 3, millions of writes per second in total; exclusive host; master 730d384, branch = luc-gvn 2bcad2f):

| writes | threads | CPython 3.14t | master | branch |
|---|---:|---:|---:|---:|
| global counter += 1 (module dict watched) | 1 | 16.57 | 10.04 | 7.88 |
| | 2 | 16.42 | 9.40 | 7.16 |
| | 4 | 14.88 | 8.28 | 6.01 |
| | 8 | 8.07 | 4.22 | 3.28 |
| Cls.x += 1, a class per thread | 1 | 7.52 | 1.75 | 1.75 |
| | 2 | 12.97 | 2.60 | 2.60 |
| | 4 | 10.32 | 3.72 | 3.76 |
| | 8 | 4.15 | 4.52 | 4.47 |

Global stores lose 21-24% at every thread count to the lock and log; class-attribute writes are dominated by lu_type_modified itself (subclass snapshot, 4x slower than CPython on master already), so the lock does not show there yet.

## Acceptance Criteria

A store to a watched dict or a type change takes no process-wide lock when no unit depends on what changed (for example a per-object filter checked lock-free, and per-bucket locks with a log that writers append to without one shared mutex); the global-counter rows match master at 1-8 threads; the C11 argument in deps.c still holds.

