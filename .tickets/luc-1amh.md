---
id: luc-1amh
status: open
deps: []
links: []
created: 2026-11-06T04:35:22Z
type: bug
priority: 2
assignee: Greg Wedow
---
# park_locks times out silently under host load

Seen twice on luc-6e6n (0614655) make check, once plain lane once jit lane, at load average ~15: timeout with no output. Not reproduced: 8/8 kernelcheck, 150/150 parallel, same counts on master 477c2ed. Probably a rare hang (lost wakeup?) independent of that branch.

