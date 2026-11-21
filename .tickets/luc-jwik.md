---
id: luc-jwik
status: open
deps: []
links: []
created: 2026-11-21T09:52:03Z
type: task
priority: 1
assignee: Greg Wedow
---
# startup: the _abc port's register and subclass checks cost 10-35x CPython

luc-w2ra attribution (11-21): a class with ABCMeta costs 752k instructions vs CPython's 117k; ABC.register is 10-35x CPython. refs_alive() builds a fresh set on every check. Make the registry/cache checks allocation-free on the hit path, keeping semantics identical (weak references, cache invalidation counter). Measure ABC class creation, register and isinstance against an ABC before and after.

