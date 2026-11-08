---
id: luc-gk3v
status: open
deps: []
links: []
created: 2026-11-08T10:05:25Z
type: bug
priority: 2
tags: [jit, perf]
---
# jit: a guard at a no-speculate site keeps failing in pyperformance go (Square.find@2)

Found by luc-xboo: after Square.find@2 (LOAD_ATTR self.reference) is marked no-speculate, units compiled since still take guard exits attributed to find@2 (op 80, depth 1), and the dumped IR shows no guard there, so it is one the optimizer moves or merges onto that state. Master hid it behind MAX_COMPILES; luc-xboo's escalation bounds it at ~12 recompiles. Find the guard (LUCIFER_JIT_DUMP with LUCIFER_JIT_ONLY=Square.find on bm_go) and make the mark reach it. Same shape at Board.useful_fast@2.

