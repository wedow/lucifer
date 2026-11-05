---
id: luc-qbja
status: open
deps: []
links: []
created: 2026-11-05T18:48:09Z
type: bug
priority: 2
assignee: Greg Wedow
---
# rare timeout of a one-line error program under jit=always

tests/kernel/err_str_removeprefix_type.py ('a'.removeprefix(1)) timed out once in make check's jit lane (-X lucifer_jit=always) on the luc-jeec round-1 trial merge (4d2c18e) under heavy host load. 0/380 on rerun, including 8 parallel loops; not hash-seed dependent (seeds 0-400 clean) and not the luc-cvgs dict probe loop. Suspects: startup, error-path or park/timer race under load. Next occurrence: capture a core (ptrace attach is blocked; use a core dump) before rerunning.

