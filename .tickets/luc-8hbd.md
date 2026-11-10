---
id: luc-8hbd
status: open
deps: []
links: []
created: 2026-11-10T03:47:41Z
type: bug
priority: 3
tags: [stdlib, io]
---
# io: BufferedReader.seek does not check raw.seekable()

Modules/_io/bufferedio.c's seek raises UnsupportedOperation('File or stream is not seekable.') when raw.seekable() is false; lib/modules/_io.py (from _pyio) seeks anyway. test_zstd FileTestCase.test_seek_not_seekable fails on it; bz2/lzma file objects share the path (compression._common._streams).

