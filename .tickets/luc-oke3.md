---
id: luc-oke3
status: open
deps: []
links: []
created: 2026-11-10T05:41:04Z
type: bug
priority: 3
parent: luc-zelv
---
# bytearray.extend twin slower than master

k_bytearray_extend 181 ns/op on master, 226 with the fixed-arity twin (_extend) of luc-zelv stage 3 (CPython 57). Find what the twin's inline does worse than the generic call.

