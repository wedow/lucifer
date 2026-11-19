---
id: luc-3b1b
status: open
deps: []
links: []
created: 2026-11-19T04:29:01Z
type: bug
priority: 3
---
# array resize while exported: BufferError text is bytearray's

a = array('d', [1.0]); m = memoryview(a); a.append(2.0) raises BufferError('Existing exports of data: object cannot be re-sized') (the bytearray storage's); CPython raises BufferError('cannot resize an array that is exporting buffers'). Every resizing array method (append, extend, pop, insert, remove, del, +=, *=, frombytes, fromlist) words it so. Pre-existing on master 7937512e; tests/kernel/jit_typed_items.py prints only the type.

