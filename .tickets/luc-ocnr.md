---
id: luc-ocnr
status: open
deps: []
links: []
created: 2026-11-08T23:25:28Z
type: bug
priority: 3
tags: [stdlib]
---
# array: an array is a bytearray instance

lib/modules/array.py makes array a bytearray subclass, so isinstance(array('Q', ...), bytearray) is True and code that takes len() of a bytearray as its byte count is wrong: test_bz2 test_issue44439 (BZ2File.write returns 5, not 40, for array('Q', [1, 2, 3, 4, 5])). CPython's array is not a bytearray.

