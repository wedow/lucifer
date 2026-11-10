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


## Notes

**2026-11-10T05:18:05Z**

luc-clibs built termios, _lzma, _zstd, _dbm, _gdbm, _curses, _curses_panel, readline, syslog and _uuid from Modules/ (plus os.openpty and os.login_tty, input() through PyOS_Readline). Lib/test PASS, master -> branch (3.14t): test_termios 0->21 (21), test_lzma 0->119 (120), test_zstd 0->117 (121), test_dbm_gnu 0->19, test_dbm_ndbm 0->16, test_shelve 266->434 (434), test_curses 0->100 (100), test_readline 0->15 (16), test_uuid 50->105 (105), test_syslog 0->9 (13, subinterpreters), test_tarfile 556->758 (764), test_zipfile 316->380 (385), test_pty 0->6, test_tty 0->4, test_cmd 5->7, test_asyncio.test_events 0->194 (309; signal.set_wakeup_fd, sockets). Still missing from this ticket's list: _hashlib/_hmac, _sysconfig, _lsprof, _remote_debugging, _statistics, xxlimited.
