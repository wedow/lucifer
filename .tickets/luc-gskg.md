---
id: luc-gskg
status: open
deps: []
links: []
created: 2026-11-06T01:23:01Z
type: bug
priority: 2
tags: [tracing]
---
# pdb: compile() from codeop._maybe_compile segfaults; libtest test_pdb 152 CRASH

Found while closing luc-d5u1. `bin/lucifer -X lucifer_jit=off -m test test_pdb` dies with SIGSEGV in compile() called from codeop._maybe_compile (pdb._read_code -> codeop.compile_command), so every test_pdb row is CRASH in every JIT mode (oracle: 152 PASS). test_monitoring is 50/89 PASS and test_sys_settrace 418/448 in every JIT mode, under the M0.3 bar of 90%; the PASS sets do not change with -X lucifer_jit=always.

