---
id: luc-hij3
status: open
deps: []
links: []
created: 2026-11-07T23:49:21Z
type: bug
priority: 3
tags: [capi]
---
# kernel slot wrappers word a keyword argument's TypeError differently from CPython

int.__add__(1, 2, x=3) and str.__len__('a', x=3) raise "int.__add__() got an unexpected keyword argument 'x'" where CPython 3.14 says "wrapper __add__() takes no keyword arguments". Kernel wrapper descriptors (kernel_wrapperbase, PyWrapperFlag_KEYWORDS) pass keywords to the kernel function. Found writing tests/kernel/wrapper_descr_calls.py (luc-ibwc), which leaves the case out.

