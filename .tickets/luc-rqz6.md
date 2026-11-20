---
id: luc-rqz6
status: closed
deps: []
links: []
created: 2026-11-19T12:53:22Z
type: bug
priority: 2
---
# TextIOWrapper loses writes from concurrent threads under jit-deopt

test_io CIOTest.test_write_readline_races (2 writer threads x 100 writes of 'x' to one text file, a reader iterating it) under -X lucifer_jit=always -X lucifer_stress=deopt:7: the file ends 189-198 bytes long, not 200, on master ba3426d6 8/8 runs (python -m test -m test_write_readline_races test_io). The off floor passes it. Lost writes: a TextIOWrapper/BufferedRandom write path that is not atomic under compiled code with failing guards (a store or a pending-buffer update replayed or skipped at a deopt exit?).


## Root cause

Not compiled code: a data race in lib/modules/_io.py. TextIOWrapper keeps encoded writes back in _pending (textio.c's pending_bytes), and read, readline, tell and seek flush them first; CPython runs both under the TextIOWrapper's critical section, this port under no lock. A reader's _writeflush joined the list, a writer appended to it, and the reader replaced it with an empty list: the write was gone. The test fails in every mode on master 1929ea93: 10/10 with -X lucifer_jit=off, 4/6 with -X lucifer_jit=always, 3/4 and 4/6 under jit-deopt (the off floor's single pass was luck).

## Fix

The pending text is one object (_Pending: items, count, flush lock, holder). Writers only append; a flush takes the items there, deletes just those and writes them out under the flush lock, so every write reaches the buffer once and flushes stay in order. A write that needs no flush takes no lock. Regression: tests/kernel/threads_text_write_races.py (the test_io race, five rounds of 2 x 400 writes): master writes short in 9 of 10 runs with the tier off; fixed 0/10 with the tier off, 0/10 default, 0/10 jit-deopt; test_io's test 0/10 jit-deopt. The decoder and snapshot state stay unlocked as before (concurrent readers of one text stream).
