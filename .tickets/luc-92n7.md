---
id: luc-92n7
status: open
deps: []
links: []
created: 2026-11-11T05:06:43Z
type: bug
priority: 2
tags: [conformance, fuzzdiff, capi]
---
# C-API: PyMemoryView_GetContiguous is unimplemented and aborts

Found by `make fuzzdiff` (tools/fuzzdiff): bytesobj_diff fn mem.script, 4 cases at seed 1, 1,000 cases (exit -6). The generator's 'contig' op calls PyMemoryView_GetContiguous(obj, buffertype, order), and Lucifer aborts with 'unimplemented C-API function PyMemoryView_GetContiguous'. CPython: Objects/memoryobject.c PyMemoryView_GetContiguous (a view of the buffer, or a contiguous copy in the requested 'C', 'F' or 'A' order; BufferError for a writable request that needs a copy).

