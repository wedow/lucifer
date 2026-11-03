---
id: luc-i53y
status: open
deps: []
links: []
created: 2026-11-03T08:13:00Z
type: task
priority: 2
parent: luc-vvqv
---
# weakrefs need the free-threaded try-incref once threads start

weakref_get counts a referent alive while its refcount is above zero, under a striped mutex; exact only while one thread runs. With threads it needs the biased-count try-incref of cpython (a shared flag on objects with weakrefs). Also _warnings is a stub that drops ignored categories and writes the rest to fd 2; replace it when the warnings module imports.

