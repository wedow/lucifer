---
id: luc-pfnu
status: closed
deps: []
links: []
created: 2026-11-08T18:17:46Z
type: task
priority: 1
tags: [perf, runtime]
---
# tuple hash: native, cached in ob_hash

tuple.__hash__ builds a list of element hashes in kernel Python and folds it with xxh_hash: 81% of mdp. CPython 3.14 also caches a tuple's hash in ob_hash, which lucifer does not (an element whose __hash__ changes is observable). Hash natively, exact xxHash values, cache in ob_hash. Census 2026-11-08 mechanism 9.


## Notes

**2026-11-08T19:52:12Z**

Landed on luc-hotc: lu_tuple_hash in C (exact str/int/bool/float/bytes/None/tuple items natively, others through PyObject_Hash), cached in ob_hash as 3.14 does; hash_of and tuple.__hash__ call the tuple_hash primitive. mdp vs CPython 3.14t: 46.42x -> 5.42x (8.6x faster). Test: tests/kernel/tuple_hash.py (caching, wide __hash__ results, errors).
