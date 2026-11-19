---
id: luc-rqz6
status: open
deps: []
links: []
created: 2026-11-19T12:53:22Z
type: bug
priority: 2
---
# TextIOWrapper loses writes from concurrent threads under jit-deopt

test_io CIOTest.test_write_readline_races (2 writer threads x 100 writes of 'x' to one text file, a reader iterating it) under -X lucifer_jit=always -X lucifer_stress=deopt:7: the file ends 189-198 bytes long, not 200, on master ba3426d6 8/8 runs (python -m test -m test_write_readline_races test_io). The off floor passes it. Lost writes: a TextIOWrapper/BufferedRandom write path that is not atomic under compiled code with failing guards (a store or a pending-buffer update replayed or skipped at a deopt exit?).

