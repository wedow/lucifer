---
id: luc-n6cx
status: open
deps: []
links: []
created: 2026-11-04T04:33:39Z
type: bug
priority: 1
---
# Kernel dict delete/store commit by index after an unlocked find: a resize in between removes or replaces the wrong key

Review of luc-njhp, reproduced: four threads calling __dict__.pop lose a key. delete/store find the index without the lock, then dict_remove/dict_replace check only that the entry is live. Revalidate the key and table under the critical section at commit, or do the whole find-and-commit under it.

