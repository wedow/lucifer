---
id: luc-acqf
status: open
deps: []
links: []
created: 2026-11-06T01:34:14Z
type: feature
priority: 2
---
# _struct.Struct with CPython's object layout, so the oracle's _testbuffer loads

The oracle-built _testbuffer reads struct.Struct internals directly (PyPartialStructObject: s_size, s_len after the header, Modules/_testbuffer.c get_nmemb). lucifer's _struct is Python, so ndarray([1], (1,)) reads garbage as s_len and allocates without bound (killed at the memory cap). Blocks test_capi.test_number test_misc_add/test_misc_multiply (oracle PASS, SKIP here without _testbuffer) and every libtest module that imports _testbuffer (test_buffer, test_memoryview parts). Once Struct has the layout, add _testbuffer to LIBTEST_EXTS in the Makefile.

## Acceptance Criteria

_testbuffer in LIBTEST_EXTS; test_capi.test_number 11/11; ndarray constructs.


## Notes

**2026-11-09T09:18:51Z**

Full Lib/test run 2026-11-09: 78 cases wait on _testbuffer (test_buffer 76 'ndarray object required', test_capi 2).
