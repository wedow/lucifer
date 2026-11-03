---
id: luc-mfrx
status: open
deps: []
links: []
created: 2026-11-03T09:00:47Z
type: task
priority: 2
tags: [runtime, kernel]
---
# K1o follow-ups: finalization of unclosed files, TextIOWrapper speed, small codec gaps

(1) A file object nobody closed loses its buffer at exit: CPython flushes it when module teardown frees it. Needs module teardown or an _io registry flushed by Py_FinalizeEx, after atexit. (2) print costs about 25 us per TextIOWrapper.write on the interpreter (100000 print calls: 10 s against 0.08 s); the write path wants a primitive or the JIT. (3) utf-7 codec, and the namereplace handler needs unicodedata names. (4) _warnings drops DeprecationWarning, so _codecs.unicode_escape_decode's invalid-escape warning is silent. (5) AttributeError names C types by tp_name ('_io.FileIO' object has no attribute), ports name them FileIO. (6) UnicodeTranslateError.__str__ prints '\x61' for 'a'. (7) input() has no tty/readline path. (8) sys.excepthook is not called for uncaught exceptions: pylifecycle report_uncaught prints directly.

