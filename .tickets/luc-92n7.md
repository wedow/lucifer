---
id: luc-92n7
status: closed
deps: []
links: []
created: 2026-11-11T05:06:43Z
type: bug
priority: 2
tags: [conformance, fuzzdiff, capi]
---
# C-API: PyMemoryView_GetContiguous is unimplemented and aborts

Found by `make fuzzdiff` (tools/fuzzdiff): bytesobj_diff fn mem.script, 4 cases at seed 1, 1,000 cases (exit -6). The generator's 'contig' op calls PyMemoryView_GetContiguous(obj, buffertype, order), and Lucifer aborts with 'unimplemented C-API function PyMemoryView_GetContiguous'. CPython: Objects/memoryobject.c PyMemoryView_GetContiguous (a view of the buffer, or a contiguous copy in the requested 'C', 'F' or 'A' order; BufferError for a writable request that needs a copy).


## Notes

**2026-11-21T11:15:08Z**

The new tests/kernel/memoryview_get_contiguous.py check reproduced the unimplemented C-API abort on the base revision. The implementation shares compatible managed exports and makes read-only copies in the requested C or Fortran layout, preserving shape and format. The shared copy path follows buffer suboffsets and raises ValueError for a NULL item pointer. The new check and three neighboring memoryview checks matched CPython 3.14t in plain, jit, and jit-deopt. The ABI audit and the NULL item pointer check passed.
