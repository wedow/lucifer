---
id: luc-eiti
status: open
deps: []
links: []
created: 2026-11-04T13:15:45Z
type: bug
priority: 3
parent: luc-h4gc
---
# Python-defined __release_buffer__ is never called (mmap.close ignores exports)

slot_bf_getbuffer returns the memoryview's own buffer, so a class that defines __release_buffer__ (PEP 688) is never told of a release. mmap.close() therefore cannot raise BufferError while exports exist (numpy test_mmap_close). Fix: keep the exporter as view->obj, hold the returned memoryview in view->internal, and run __release_buffer__ from a bf_releasebuffer thunk.

