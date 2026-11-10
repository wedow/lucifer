---
id: luc-x2i0
status: open
deps: []
links: []
created: 2026-11-09T09:23:30Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: test_ctypes fails to load: Structure's base is _CStruct, not _CData (582 cases)

Full Lib/test run, 2026-11-09: test_ctypes (582 cases CPython 3.14t passes) fails at load. ctypes.wintypes needed the 'v' type code and _ctypes.CTYPES_MAX_ARGCOUNT, both added in the libtest lane; the next blocker is test/test_ctypes/_support.py asserting Structure.__base__.__name__ == '_CData'. Lucifer's Structure and Union derive from a shared _CStruct; in CPython both derive from _CData directly. Fold _CStruct into the two classes (or into _CData), then rerun test_ctypes for its real failures.

