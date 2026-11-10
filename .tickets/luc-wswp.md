---
id: luc-wswp
status: open
deps: []
links: []
created: 2026-11-10T02:35:04Z
type: task
priority: 2
parent: luc-zelv
---
# Call arms on type(x) miscompile kernel exec (unexplained)

Customizing builtin calls by argument classes (luc-zelv stage 3) broke exec(code, globals) under -X lucifer_jit=always when the arms covered type(x) calls inside inlined kernel frames (truth, _source): the result of _args.unpack reached UNPACK_SEQUENCE as a 2-tuple. type() is now excluded from the arms (it is a load anyway), which fixes it, but the arms machinery for one-argument calls may hide a real defect: find it. Repro: exec(probe.__code__, g) in a loop at module level with only exec compiled (LUCIFER_JIT_ONLY=exec).

