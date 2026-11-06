---
id: luc-n05c
status: closed
deps: []
links: []
created: 2026-11-06T15:16:01Z
type: task
priority: 1
assignee: Greg Wedow
---
# JIT: inline str_ord/str indexing and small Python calls in character loops

After luc-pcs5/luc-v1cz, _sre and _json are 9-60x slower than CPython. A str_ord loop costs ~40-60 ns/char and a small Python call ~150 ns; both dominate. Inline the str read primitives and small calls in the tier; measure the _sre microbenchmarks and json dumps/loads before and after. Also: DELETE_SUBSCR and the LIST_EXTEND in _args.unpack side-exit on every run.

