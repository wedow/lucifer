---
id: luc-oq8q
status: open
deps: []
links: []
created: 2026-11-04T13:20:22Z
type: bug
priority: 3
parent: luc-h4gc
---
# ctypes objects export a PEP 3118 format for pointers, structures and wchar

memoryview(ctypes.pointer(c_uint8())).format must contain '&'; memoryview of a c_wchar array and of a Structure must carry CPython's format ('u', 'T{...}'), so numpy sees structured fields (np.asarray(Structure)['a'], and rejects wchar with ValueError). _ctypes._buffer_format knows only arrays of simple types, and memoryview.cast cannot build those formats: needs a primitive that makes a view with an arbitrary format string. 3 numpytest rows (TestNewBufferProtocol).

