---
id: luc-a7v4
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: feature
priority: 2
tags: [libtest]
---
# libtest: stdlib extension modules not built (1656 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. Missing modules, by Lib/test cases lost: termios 548 (test_asyncio.test_events 309, test_pyrepl 204, test_termios 21, test_pty), _zstd 256 and _lzma 255 (test_zstd 121, test_lzma 120, test_tarfile 202, test_zipfile 66), _dbm/_gdbm (test_shelve 168, test_dbm_gnu 19, test_dbm_ndbm 16), _curses 100, _uuid 55, _hashlib and _hmac 60 (test_hmac), _sysconfig 36, _lsprof 41 (test_cprofile 27, test_pstats 13), _remote_debugging (test_asyncio.test_tools 22), _statistics 20, readline 16, xxlimited 16, syslog. Add each to STDLIB_EXTS (sources need vendoring first).

