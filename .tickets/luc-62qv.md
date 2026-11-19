---
id: luc-62qv
status: open
deps: []
links: []
created: 2026-11-19T09:03:54Z
type: task
priority: 3
---
# memoryview items are not lowered in line

Gap report 2026-11-18 mechanism #5 named memoryview get/set; luc-cxwo covers array and bytearray only. mv[i] runs the kernel memoryview.__getitem__ (mv_attr, _lookup, _item -> mv_item prim). A 1-D C-contiguous view of a native format can read view.buf + i * itemsize under the same guards as TYPED_LOAD, with the release state and the exporter's export count making the buffer stable (a view's buffer cannot be resized while exported). Profile base64/pickle first: on 2026-11-19 their cost was slicing, struct and the _json scanner, not item access.

