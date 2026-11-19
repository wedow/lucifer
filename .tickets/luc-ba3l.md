---
id: luc-ba3l
status: closed
deps: []
links: []
created: 2026-11-19T09:52:18Z
type: bug
priority: 2
---
# __index__ results are checked where lib modules convert indexes

array's _index (and the copies in itertools, _collections, _ctypes, mmap, math) call __index__ without checking its result: array('i',[10,20,30])[F()] with __index__ returning 2.7 reads item 2. CPython's PyNumber_Index raises TypeError: __index__ returned non-int (type float), and warns (DeprecationWarning) on an int-subclass result. Use the kernel's index (_operator.index) everywhere.

