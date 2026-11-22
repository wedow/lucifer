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


## 2026-11-22: per-opcode table (origin/master 8c0ad0cc)

Executed machine instructions per inner iteration, by IR opcode of the hot unit, counted exactly by single-stepping steady-state iterations (all iterations of a row identical but the outer loop's). "C" is code a fast path calls; only listidx calls C on its hot path. CMP_I + BRANCH is the compare-and-branch pair.

| opcode | loads | stores | pstores | listsum | listidx | calls | tupunpack | floatloop |
|---|---|---|---|---|---|---|---|---|
| total | 167 | 141 | 110 | 73 | 279 | 104 | 141 | 54 |
| INLINE_LOAD_I | 84 (4x21) | | | | | 21 | | |
| INLINE_STORE | | 70 (2x35) | 47 | | | | | |
| LIST_STORE | | | | | 74 + 66 C | | | |
| LIST_ITEM | 20 | 20 | 20 | 20 | 21 | 20 | 20 | |
| DECREF | 16 | 13 | 13 | 15 | 30 | 16 | 28 | 6 |
| UNBOX_INT | | | | 12 | 12 | | 26 | |
| BOX_INT | | | | | 32 | | | |
| ADD_I / SUB_I | 21 | 5 | 5 | 9 | 5 | 9 | 13 | |
| CMP_I + BRANCH | 6 | 6 | 6 | 6 | 2 | 6 | 11 | 2 |
| JUMP (edge moves) | 8 | 5 | 5 | 5 | 5 | 8 | 12 | 7 |
| GUARD_* | 6 | 16 | 8 | | 11 | 8 | 7 | |
| LOAD | 3 | 3 | 3 | 3 | | 3 | 9 | |
| POLL | 3 | 3 | 3 | 3 | 3 | 6 | 3 | 3 |
| INCREF | | | | | | | 12 | |
| float ops, I2F | | | | | | | | 21 |
| RITER_*, PHI | | | | | 18 | | | 15 |
| code bytes | 6861 | 8021 | 5476 | 6057 | 9082 | 6296 | 7730 | 7464 |

while_attr is left out: its Node class is made per call, so the traced call runs a fresh class through exits, not the steady state.

Reading: the emitter bodies of the object protocol (attribute load 21, attribute store 35, list item 20, release 8-15, unbox 12, box 32) dominate; the compare/branch pair, the poll, edge moves and constant operands are 3-12 per row together.

## 2026-11-22: stage a, registers kept across slow-path calls

Releases and truth tests call C only on a slow path, which now keeps every register (a published state reads them where the slow path saved them), so they no longer count as calls to the allocator; a call clobbers only what is live across it (backward liveness per block, not the interval's span); an attribute load from a dict, which calls nothing, is not a call. Stores (INLINE_STORE*, LIST_STORE) are untouched while the lock elision changes them.

| row | instructions before | after | cycles before | after | code bytes before | after |
|---|---|---|---|---|---|---|
| loads | 167 | 157 | 60, 71, 56 | 64, 49, 53 | 6861 | 6674 |
| tupunpack | 141 | 132 | 42, 44, 42 | 47, 46, 46 | 7730 | 7572 |
| floatloop | 54 | 49 | 32 | 24 | 7464 | 7391 |
| calls | 104 | 102 | 43 | 42 | 6296 | 6192 |
| listidx | 279 | 277 | | | 9082 | 9106 |
| listsum | 73 | 73 | 27 | 26 | 6057 | 5960 |
| stores | 141 | 141 | 122 | 119 | 8021 | 8103 |
| pstores | 110 | 110 | | | 5476 | 5611 |

tupunpack loses about 9% of cycles with 9 fewer instructions and no worse code on its path (the loop's values moved from frame words to registers); the likely cause is layout of a loop whose slow paths sit in line, which stage b changes.

## 2026-11-22: stage b, slow paths out of line; branches kept within 32-byte windows

Slow paths go to a second code stream that ends the unit, so fast paths fall through. Alone that made tuple unpacking about 9% slower: counters showed the loop decoded by the legacy decoders (70-80 uops per iteration from MITE, 15-40 from the uop cache), the JCC erratum's microcode fix keeping no branch that crosses or ends at a 32-byte boundary in the uop cache. Branches (with the compare they fuse with) are now moved to the next window by NOPs: tupunpack 120 of 120 uops from the uop cache, 3 cycles of 67 undelivered. The NOPs count as instructions (loads +22, others +6 to +17).

Cycles per iteration, interleaved min of 3, origin/master vs stages a+b:

| row | cycles before | after | instructions before | after | code bytes before | after |
|---|---|---|---|---|---|---|
| tupunpack | 47 | 30 | 141 | 147 | 7730 | 8023 |
| loads | 65 | 37 | 167 | 177 | 6861 | 7078 |
| listsum | 22 | 16 | 73 | 84 | 6057 | 6298 |
| calls | 39 | 22 | 104 | 112 | 6296 | 6568 |
| stores | 123 | 115 | 141 | 158 | 8021 | 8546 |
| pstores | 66 | 63 | 110 | 121 | 5476 | 5926 |
| floatloop | 27 | 26 | 54 | 54 | 7464 | 7820 |
| listidx | 115 | 116 | 279 | 287 | 9082 | 9640 |

## 2026-11-22: stages c+d, compare fused with its branch; immediates; results in place

A compare (CMP_I, IS, RITER_DONE, ordered CMP_F) whose one reader is the next instruction, a BRANCH or a GUARD_BIT, leaves its condition in the flags (cmp, jcc instead of cmp, setcc, movzx, mov, test, jcc); constant operands that fit are immediates; ADD/SUB/MUL/AND/OR/XOR_I, float arithmetic and LOAD make their result in its own register.

| row | instructions after b | after c+d | cycles after b | after c+d |
|---|---|---|---|---|
| tupunpack | 147 | 132 | 31 | 28 |
| floatloop | 54 | 48 | 22 | 14 |
| loads | 177 | 169 | 49 | 31 |
| listsum | 84 | 77 | 17 | 16 |
| calls | 112 | 103 | 21, 35, 24 | 26, 21, 25 |
| stores | 158 | 153 | 120 | 115 |
| pstores | 121 | 115 | 64, 67, 66 | 68, 64, 62 |
| listidx | 287 | 284 | 121 | 122 |

calls swings between runs on both arms (21 to 35 cycles); counters taken in one lease: 24.8 cycles after b, 21.5 after c+d.

## 2026-11-23: stage f, phis and their inputs share a register; e not done

A phi and its inputs hint at each other in the linear scan: an input takes the phi's register when it is free, or when its instruction reads the phi as operand a for the last time and may write its result over it (an add or subtract undoes itself on overflow before the exit reads the operand). It fires on the sums and the iterator index of loads, listsum and tupunpack; values whose interval runs past their last use (a later block in the layout) still move. The one-compare POLL (e) needs the poll word at a fixed place from the task, which it is not (task -> carrier -> word): not a few lines, left.

| row | instructions after c+d | after f |
|---|---|---|
| loads | 169 | 162 |
| listsum | 77 | 74 |
| tupunpack | 132 | 125 |
| calls | 103 | 104 |
| pstores | 115 | 116 |
| stores, listidx, floatloop | 153, 284, 48 | 153, 284, 48 |

Counters in one lease, min of 3 (cycles): tupunpack 25.5 after c+d, 24.4 after f; listidx 125.3 master, 124.5 b, 122.0 c+d, 118.6 f.

## 2026-11-23: all stages against origin/master

| row | instructions master | now | cycles master | now (one lease, interleaved min of 3) | code bytes master | now |
|---|---|---|---|---|---|---|
| loads | 167 | 162 | 68 | 25 | 6861 | 6996 |
| tupunpack | 141 | 125 | 49 | 31 | 7730 | 7906 |
| calls | 104 | 104 | 39 | 22 | 6296 | 6472 |
| listsum | 73 | 74 | 22 | 19 | 6057 | 6303 |
| floatloop | 54 | 48 | 31 | 25 | 7464 | 7820 |
| stores | 141 | 153 | 121 | 118 | 8021 | 8517 |
| pstores | 110 | 116 | 66 | 64 | 5476 | 5859 |
| listidx | 279 | 284 | 115 | 129 (118.6 in the counter lease) | 9082 | 9640 |

Instructions include the NOPs that keep branches inside 32-byte windows (6 to 22 per iteration); code bytes grow by them as well. Cycle numbers move by 10-20% between leases on the same binary (calls 21-35); compare within a row of one lease.

## 2026-11-23: review fix, held values after a published slow path

Defect (stage a): after a published slow-path call that saves the registers but is not a park (TRUTH imm != 1, DECREF imm 1), unpublish dropped the frame slots' holds before the registers came back, reading each held value from a register C had clobbered: the decref hit garbage and the real object leaked the reference taken before the call. tests/kernel/jit_published_holds.py shows it (truth refcount delta 2244 to 239646 instead of 0 in plain, jit and jit-deopt); passes on master and now. Fix: one rule for where a value is during a published slow path, the walkers' own: when the slow path saved the registers, unpublish reads a held value in a register from its word at jit_regs.

Sites that reach unpublish after a C call, and where held values are then:
- call_helper (HELPER, a call to the allocator): the state's values live across the call, so in a preserved register (kept by the C ABI) or a spill word.
- kept_end (TRUTH imm != 1, DECREF imm 1): in the register save area at jit_regs, read from there (the fix).
- parked_end (LIST_ITEM, INLINE_LOAD box, POLL): a park takes no holds (nheld 0).
- decref without keep (the old value an INLINE_STORE*/LIST_STORE releases), inline_store_raw, LIST_STORE, INLINE_STORE slow paths: those are calls to the allocator that clobber the preserved registers, so every value live across them is in a spill word.

Holds at a release: none can exist, since a release's state cannot name the object it releases (a walker would read a freed object). Probed: every published slow path that took holds, over all tests/kernel jit, finalizer and truth programs compiled at first call, was a TRUTH (153 sites), none a DECREF. The test's release case (an alias that outlives the released name, a finalizer that allocates and calls) passes in the three lanes, as does a __bool__ that writes the caller's locals through f_locals in a loop with no call.

Branch windows (stage b) are padded only on the cores the JCC erratum fix covers (CPUID: Intel family 6, models 4E 5E 55 8E 9E A5 A6), decided once; elsewhere no NOPs. Prefix padding instead of NOPs needs instruction starts the encoder does not keep: left.
