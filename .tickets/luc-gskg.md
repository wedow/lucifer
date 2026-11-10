---
id: luc-gskg
status: closed
deps: []
links: []
created: 2026-11-06T01:23:01Z
type: bug
priority: 2
tags: [tracing]
---
# pdb: compile() from codeop._maybe_compile segfaults; libtest test_pdb 152 CRASH

Found while closing luc-d5u1. `bin/lucifer -X lucifer_jit=off -m test test_pdb` dies with SIGSEGV in compile() called from codeop._maybe_compile (pdb._read_code -> codeop.compile_command), so every test_pdb row is CRASH in every JIT mode (oracle: 152 PASS). test_monitoring is 50/89 PASS and test_sys_settrace 418/448 in every JIT mode, under the M0.3 bar of 90%; the PASS sets do not change with -X lucifer_jit=always.


## Notes

**2026-11-09T09:18:51Z**

Still zeroes test_pdb (152 cases) in the full Lib/test run of 2026-11-09.

**2026-11-10T03:44:42Z**

Root cause: _IncompleteInputError had no dealloc (it was left out of the exception type setup), so codeop's dropped incomplete-input error crashed on release. Fixed on luc-crash; test_pdb runs whole: 135 of 152 PASS (oracle 152).
