---
id: luc-gdfk
status: open
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 2
tags: [capi]
---
# msgpack aborts on a non-contiguous memoryview

ft-packages msgpack serial lane: test/test_memoryview.py::test_unpack_noncontiguous_memoryview aborts inside _cmsgpack calling into bin/lucifer (C stack: _cmsgpack -> lucifer +0x85fc0 -> +0x92de1 -> abort), so all 161 msgpack cases are CRASH. Likely PyObject_GetBuffer/PyBuffer_* on a strided memoryview.

