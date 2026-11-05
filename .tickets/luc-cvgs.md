---
id: luc-cvgs
status: closed
deps: []
links: []
created: 2026-11-05T17:49:59Z
type: bug
priority: 1
assignee: Greg Wedow
---
# kwargs_str_subclass hangs under jit=always deopt:7

tests/kernel/kwargs_str_subclass.py (extended by luc-kil1, ac81ffd) times out intermittently under -X lucifer_jit=always -X lucifer_stress=deopt:7: 7 of 40 runs on master 389dec7. It is in make check's jit-deopt lane, so it flakes the gate. Find whether it is a real hang (deopt during a user __eq__ called from keyword binding in runtime/interp/call.c find_parameter, or during DICT_MERGE duplicate detection) and fix the runtime, not the test.

