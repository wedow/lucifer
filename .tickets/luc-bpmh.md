---
id: luc-bpmh
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit backend: instructions per IR instruction (registers across slow paths, out-of-line slow paths, fused compare and branch)

Measured on IR that is already close to ideal: for v in xs: s += v is 11 IR instructions in the loop and 73 machine instructions per iteration (PyPy 17); reading 4 int attributes 167 (PyPy 35); one object-attribute store about 37 on its hot path with the locks already gone.

What the disassembly shows, each general:
- A value live across an instruction that has a C slow path is kept in a frame slot and reloaded at every use (mov rdx,[rbp-N] before each op). The fast path should keep it in a register; the slow path saves and restores around its call.
- Slow paths are emitted in line, so the hot path jumps over them. Emit them after the unit's hot code.
- CMP_I then BRANCH is cmp, setcc, movzx, mov, test, jcc. Fuse to cmp, jcc when the compare's only use is the branch.
- The same flag byte is tested twice in INLINE_STORE; LIST_ITEM checks the index against the size and again against the allocation when the loop bound already proves the first; an ADD_I by a constant loads the constant into a register.
- POLL is four instructions with two loads; one compare of a memory word against zero suffices on the fast path.
- JUMP at a loop back edge shuffles registers the allocator could have agreed on.

Acceptance: instructions per iteration on the idiom rows read-4-attributes, store rows, list sum, list update, monomorphic call, tuple unpack, float accumulate, linked-list walk each down by at least 2x with cycles down; code bytes per unit down; jitcheck lanes and the kernel programs in all three modes unchanged.

