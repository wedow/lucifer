---
id: luc-grnc
status: open
deps: []
links: []
created: 2026-11-06T01:34:28Z
type: task
priority: 3
---
# _io._BytesIOBuffer: the exporter BytesIO.getbuffer() hands its memoryview

CPython's BytesIO.getbuffer() exports through an _io._BytesIOBuffer object (Modules/_io/bytesio.c), whose getbuffer refuses the obsolete NULL view. lucifer's BytesIO exports its bytearray and has no such type, so _testcapi.test_pep3118_obsolete_write_locks (test_capi.test_misc Test_testcapi) fails with AttributeError. A stand-in type nothing uses was tried and dropped; the type should be what BytesIO.getbuffer() actually exports through.

## Acceptance Criteria

_io._BytesIOBuffer exists and BytesIO.getbuffer() exports through it; test_pep3118_obsolete_write_locks passes.

