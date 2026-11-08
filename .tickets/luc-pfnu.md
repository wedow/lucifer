---
id: luc-pfnu
status: in_progress
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 1
tags: [perf, runtime]
---
# tuple hash: native, cached in ob_hash

tuple.__hash__ builds a list of element hashes in kernel Python and folds it with xxh_hash: 81% of mdp. CPython 3.14 also caches a tuple's hash in ob_hash, which lucifer does not (an element whose __hash__ changes is observable). Hash natively, exact xxHash values, cache in ob_hash. Census 2026-11-08 mechanism 9.

