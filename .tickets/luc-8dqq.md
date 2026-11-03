---
id: luc-8dqq
status: open
deps: [luc-qidc, luc-rjvn]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1o: _io port, codecs, encodings and standard streams

lib/modules/_io.py from Lib/_pyio.py (FileIO over the bracketed posix read/write/open/lseek/close primitives, BufferedReader/Writer/Random/RWPair, TextIOWrapper with universal newlines and incremental codecs, StringIO, BytesIO, open), lib/modules/_codecs.py (utf-8, utf-16/32, latin-1, ascii, raw/unicode_escape, charmap; error handlers strict/replace/ignore/surrogateescape/backslashreplace; lookup registry), frozen codecs, encodings, encodings.aliases, encodings.utf_8, abc (via _py_abc), io. Startup imports encodings and creates sys.stdin/stdout/stderr as init_sys_streams does (stderr backslashreplace and line buffered, stdout line buffered on a tty only, -u unbuffered); print defaults to sys.stdout; flush at exit before the exit code, so interleaving of stdout and stderr matches the oracle under pipes.
Implements: DESIGN 3.3 (frozen startup set), 9.3 (startup row through stdio); FOUNDATIONS 2.13 (flush step).
Files: lib/modules/{_io.py,_codecs.py}, tools/freeze (frozen set grows).
Adds tests/kernel: stdio.py (stdout/stderr interleaving under pipes, non-ASCII output, print(file=sys.stderr), sys.stdout.write return value, flush at exit).

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="stdio" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t); every program from K1a-K1n still matches (make check-kernel).

