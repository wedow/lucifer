---
id: luc-jwik
status: closed
deps: []
links: []
created: 2026-11-21T09:52:03Z
type: task
priority: 1
assignee: Greg Wedow
---
# startup: the _abc port's register and subclass checks cost 10-35x CPython

luc-w2ra attribution (11-21): a class with ABCMeta costs 752k instructions vs CPython's 117k; ABC.register is 10-35x CPython. refs_alive() builds a fresh set on every check. Make the registry/cache checks allocation-free on the hit path, keeping semantics identical (weak references, cache invalidation counter). Measure ABC class creation, register and isinstance against an ABC before and after.


## Notes

**2026-11-21T11:55:54Z**

The base (`d1718e17`) rebuilt a set of live weak references for recursive registry
checks. This called registered classes' metaclass `__hash__` again. The new
`tests/kernel/abc_registry_lookup.py` failed on the base when that hash raised,
and passed on CPython 3.14t. It also checks positive and negative cache hits and
cache-token invalidation. `tests/kernel/abc_caches.py` covers weak cleanup.

Direct registry and cache lookups now probe the existing identity map by address
and verify the referent. Recursive registry checks snapshot the dict's weak
references before calling subclass hooks, with no rehashing of registered
classes. The regression and six nearby kernel checks passed in plain, jit and
jit-deopt.

Median user-space instructions per operation across five interleaved runs on
the test host with `PYTHONHASHSEED=0`. Both Lucifer binaries were built in the
same checkout. Counters covered the warmed operation loops with startup and
setup excluded.

| Operation | Base | With change | CPython 3.14.10t |
| --- | ---: | ---: | ---: |
| Create an `abc.ABC` subclass | 297,431 | 298,199 | 93,581 |
| Register a new class in a registry with one entry | 122,175 | 62,680 | 10,966 |
| Register an already registered class | 22,480 | 17,657 | 4,272 |
| `isinstance`, positive cache hit | 5,188 | 2,378 | 2,745 |
| `isinstance`, negative cache hit | 5,910 | 2,442 | 2,797 |
