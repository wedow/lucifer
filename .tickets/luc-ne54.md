---
id: luc-ne54
status: open
deps: []
links: []
created: 2026-11-24T04:33:51Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit: cost of leaving compiled code for C and of unit returns (HELPER, RETURN, JUMP sequences)

Steady-state cycle attribution on 14 pyperformance programs (master after field kinds, lock elision, in-line release; sampled cycles by the IR instruction whose machine code the sample fell in), share of ALL steady-state cycles spent inside compiled code at these instruction kinds:

| program | HELPER | RETURN | JUMP | INCREF+DECREF |
|---|---|---|---|---|
| pprint | 17.1 | 3.6 | 5.4 | 4.8 |
| json_dumps | 14.9 | 6.0 | 1.1 | 4.3 |
| django_template | 14.8 | 5.2 | 4.9 | 4.8 |
| json_loads | 14.7 | 7.5 | 1.6 | 4.9 |
| chameleon | 14.6 | 3.9 | 6.7 | 7.6 |
| deltablue | 11.7 | 9.3 | 2.6 | 5.0 |
| genshi | 11.2 | 2.3 | 2.2 | 4.8 |
| xdsl | 10.6 | 5.3 | 3.4 | 3.4 |
| raytrace | 10.5 | 10.8 | 9.5 | 6.8 |
| deepcopy | 9.5 | 5.6 | 3.6 | 3.0 |
| go | 8.8 | 1.1 | 3.9 | 4.5 |
| richards | 8.7 | 5.0 | 6.0 | 17.9 |
| regex_v8 | 8.2 | 3.0 | 5.4 | 6.8 |
| logging | 7.4 | 5.6 | 1.7 | 3.1 |

Mean: HELPER 11.6%, RETURN 5.3%, JUMP 4.1%. These are cycles in the compiled code AROUND the call (the time inside the C helper is counted separately): publishing the frame state, saving and restoring registers, argument moves, the checks after the call (declined, error, frames, unit alive, deopt reason); a unit's return sequence (releases of locals, unlink of the frame, the result's hand-over); and register moves at jumps and loop back edges.

Task: measure first. For the hottest units of four of these programs, exact instructions executed per HELPER by helper kind (split into before-call, the checks after, and slow paths), per RETURN and per JUMP, with counts, so the fixes are chosen by number. Then, one landing each: the fixed cost of a published call (what must be written before the call and what can be written only if the callee asks: a frame state described by a constant record plus the registers already saved, rather than stored field by field); the checks after a call folded into one test of a word the callee sets; RETURN's releases and frame hand-over; phi moves at back edges (live-range splitting so loop-carried values keep one register across the loop).
Acceptance: the HELPER, RETURN and JUMP shares on those programs halved, with whole-run cycles down accordingly.


**2026-11-24T09:30:00Z**

Measurement (no code change). Branch origin/master ae720dc1 built with two measurement-only dump lines (code bytes and the direct entry's offset, not committed); one sampled run per program (pprint, json_loads, django_template, raytrace: cycles:u every 200003, 12 timed runs, 4.4M/261k/245k/205k samples), every sample placed on the compiled instruction it hit, the code disassembled, and each IR instruction's machine code split into what it is for. Static per-instance counts over the 11 hottest units (pprint PrettyPrinter.format, _safe_repr, _format; json_loads Scanner.__call__, _scan_once; django VariableNode.render, chain.__next__, escape_filter; raytrace Sphere.intersectionTime, Scene.visibleLights, Scene.rayColour). The gdb single-step tracer was dropped (86 minutes for nothing).

First finding: the RETURN and JUMP shares of the earlier attribution are mostly misplaced. Code after a unit's last IR instruction (shared stubs) and all cold code were charged to the last IR instruction in offset order, usually a RETURN or a JUMP. Placed properly, RETURN (the epilogue: store the result, restore the depth byte, pop six registers, ret: 15 instructions) is 0.2-0.5% of cycles and JUMP (spill-slot moves) 0.4-0.6%. What was in those buckets:

| piece | pprint | json_loads | django | raytrace | instructions per execution | what it is |
|---|---|---|---|---|---|---|
| in-line release stub (DECREF's last reference: count check, owner check, type checks, free) | 5.2 (shared) | 4.0 (shared) | 2.1 (shared) | 6.2 (shared) | 16-21 | freeing; DECREF's cost, not this ticket's |
| direct-call entry stub of a unit (frame set-up when another unit calls it directly) | in shared | in shared | 1.4 | 0.9 | 107-136 | the call's entry, counterpart of RETURN; next to measure |
| save_all + restore_all of a parked call | 1.4 | 0.9 | 0.6 | 0.6 | 32 + 31 | every GPR and XMM register saved and restored around a parked call (INLINE_LOAD box, counted LIST_ITEM slow path, the poll in place, a published DECREF that frees, TRUTH) |
| cold paths | 1.8 | 0.5 | 0.4 | 1.5 | - | slow paths |
| RETURN | 0.5 | <0.3 | <0.3 | <0.3 | 15 | epilogue |
| JUMP | 0.4 | 0.6 | <0.3 | 0.5 | 2-3 | phi moves between spill slots |

HELPER sequences (share of ALL cycles; phases by the instruction's role):

| phase | pprint | json_loads | django | raytrace | mean |
|---|---|---|---|---|---|
| holds (references taken before the call) | 3.2 | 3.1 | 1.0 | 0.6 | 2.0 |
| hold releases after the call | 4.4 | 4.3 | 1.5 | 0.9 | 2.8 |
| argument set-up (stores to the stack, in-line vectorcall dispatch of HELPER call) | 4.7 | 1.5 | 0.9 | 1.6 | 2.2 |
| result and checks after the call | 1.9 | 0.4 | 0.7 | 0.7 | 0.9 |
| publish (state record address, depth, unlocked_jit) and unpublish | 1.0 | <0.3 | <0.3 | <0.3 | 0.5 |

Per instance, static fast path, mean over the hottest units (instructions):

| helper | instances | holds per call (on objects) | holds | releases | args | publish+unpublish | checks |
|---|---|---|---|---|---|---|---|
| prim | 862 | 1.7 (0.8) | 17 | 25 | 8 | 8 | 3 |
| prim_call | 199 | 3.1 (1.0) | 30 | 44 | 9 | 7 | 3 |
| call | 560 | 3.4 (1.7) | 33 | 47 | 39 | 8 | (direct path) |
| prim_words | 28 | 8.6 (1.6) | 80 | 115 | 15 | 5 | 3 |
| setitem | 14 | 3.6 (2.4) | 36 | 51 | 7 | 5 | 4 |
| list_append | 34 | 2.1 (1.5) | 22 | 32 | 6 | 6 | 4 |
| load_attr | 50 | 2.0 (0.7) | 20 | 29 | 5 | 8 | 3 |

The expected shapes do not hold: the state is already published as one constant record address (movabs + one store), and the checks after a call are one test of the result. What dominates is the holds (publish/unpublish's per-slot references for objects the call is handed, docs/design/jit-architecture.md 10): about 23 instructions per hold (a 9-instruction INCREF before, a 13-instruction DECREF after), one per further frame slot naming the object, so the same object is held 2-5 times (prim_words: 8.6 holds on 1.6 objects); 60-90% of every published helper's in-line instructions.

Proposed order (by the numbers; lines are estimates):
1. Holds by object, not by name: one count add of n before the call and one subtract of n after (with the same owner and zero checks), instead of n INCREF and n DECREF sequences. Same counts visible to C at every point, so no semantic change. Expect about 60% of holds+releases gone: ~2.9% of all cycles on these four (pprint ~4.5%, json ~4.4%). About 40 lines in emit.c (publish, unpublish, a counted incref/decref emitter); tests: the existing hold tests (refcounts seen by C during a call, sys.getrefcount through an inlined frame) plus one with an object named by several slots.
2. Parked calls save what the state needs, not every register: the registers the allocator has live at that site (GPRs holding values the published state names or that are live after), not all 16 GPR and 16 XMM. ~0.9% mean. About 60-80 lines (a per-site mask instead of the shared save_all/restore_all stubs, jit_regs layout unchanged for the walker).
3. The direct-call entry stub (107-136 instructions per direct call, ~1% on django and raytrace): measure its pieces first, the same way.
4. Only if the owner agrees to a semantic change: skip holds where the C cannot observe counts (a helper effect flag); the remaining 40% of holds. Not proposed without that decision: C that tests uniqueness (str in-place append) relies on them.
Not proposed: phi moves at JUMP (0.4-0.6%, below the noise of the others) and RETURN (0.2-0.5%).

**2026-11-24T14:00:00Z**

Step 2: steady-state cycles by purpose (values 4-23: a sampled run of 23 values less one of 3; share of ALL cycles; every machine instruction classed at the instruction: reference-count sequences by shape, a HELPER by phase, spill-slot moves, a compare to cold code or an exit inside a work instruction as a check; nops take the class of the instruction before; unclassified 0.0%). Mean over the 14 programs:

| class | mean | range |
|---|---|---|
| compiled code, total | 38.7 | 18.2 (logging) - 80.4 (richards) |
| (a) reference counting | 15.2 | 6.8 - 25.7 |
| value INCREF/DECREF / holds / hold releases / direct-call frame refs / release stub / owner checks / slow paths | 6.0 / 1.7 / 2.5 / 0.4 / 2.7 / 1.9 / 0.1 | |
| (b) call protocol | 8.6 | 4.2 - 15.3 |
| args / publish / direct dispatch / entry stub / direct return / pushes-pops / parked save_all-restore_all / result checks / epilogue / prologue | 0.8 / 1.0 / 0.5 / 0.8 / 0.8 / 0.9 / 1.2 / 1.6 / 0.4 / 0.5 | |
| (c) guards and checks | 3.4 | |
| (d) unboxing, tag tests | 1.0 | |
| (e) allocation in line (boxing 1.3, tuple/list/object 0.7) | 2.0 | |
| (f) spill 1.8, phi moves 0.6 | 2.4 | |
| (g) the program's work | 4.6 | 0.8 (genshi) - 16.9 (richards) |
| (h) slow paths not split 1.1, exits 0.2 | 1.4 | |

Python-to-Python calls in steady state (user functions; kernel calls apart): inlined 27-100% (mean 70%), in-line direct unit entry 0-65% (regex_v8 65, deltablue 40, json_loads 40, xdsl 27, pprint 26), through C into a unit (call helper or lu_eval) up to 31% (deepcopy), interpreter into a unit up to 12%, interpreted up to 10% (genshi). One direct call costs about 280 instructions end to end besides the callee's body: caller ~150 (publish 3, holds ~10 per step, arguments 2 per word, dispatch 24, return side ~60 of which two DECREFs of the frame's function and code and a take given back, releases, result check), entry stub ~109 (pushes 8, checks the interpreter makes at a call 39, frame push and activation link 11, frame fields 18 plus INCREFs of code and function 18, parameters 4 each given, counter 3), callee prologue 7, return 17.

Stage 1 built: holds by object (one count step of n per object a published call is handed; immortal, owner and zero tests kept; another thread's object or a count at or below n goes to C, lu_jit_decrefs). Steady-state instructions (values 4-13 by difference, core lease, two runs per arm): pprint -4.8%, json_loads -9.3%, django_template -5.7%, raytrace -3.1%. Idioms (instructions per iteration): strmeth 1473 -> 1406, strjoin 1080 -> 1034, dictcount 1284 -> 1263; strfmt, kwcall, starcall, isinst, calls flat.

**2026-11-24T15:30:00Z**

Review fixes to stage 1: a count step of n saturates at immortal (a local count within n of 0xFFFFFFFF ends immortal, as n steps of one did) instead of wrapping; the shared count's lock add keeps the imm8 form when n << 2 fits (n = 1 is the old code byte for byte) and notes no flags, so no padding can fall after its prefix. Stats change: a hold release that goes to C now counts one `release` C path where it counted n. New program jit_hold_paths: a callee reading and writing the caller's locals through f_locals, one rebinding a global the caller's unit relies on, a published release with the registers saved (another thread's object with a finalizer), an owned object whose last local references are the holds (its other references another thread's), a second thread counting the object during the calls. They pass on the base binary too (they guard the new paths); whether the saved-register and C release paths run with n > 1 there is by construction, not checked in the disassembly. Not compared: an object's count right after another thread dropped references to it, which differs from CPython on the base binary as well (see below).

## State (keep current)

Branch luc-ne54 on origin/master 5d2c8d9e: test commit (jit_hold_counts kernel program, the census count hold and the jitcheck lane holds, failing before: 4 steps a call) and the stage 1 change; gate passed (new program and neighbours in plain, jit, jit-deopt; thread programs in handoff; lanes holds and releases pass). Not pushed: waiting for review. Not to build here: items 2 (parked-call register saves) and 4 (holds C cannot observe). Found out of scope: jitcheck lane borrowed fails on master (about 7.4 INCREF+DECREF per iteration, allowed 0.5); sys.getrefcount of this thread's object read right after another thread that counted it up and down ended differs from CPython (base binary too; a release waiting to be merged?). Next candidates by the step 2 table: value INCREF/DECREF (6.0) and the release stub (2.7, raytrace 11.8), the remaining holds (owner checks of the remaining steps), parked save_all/restore_all (1.2), the direct call (~280 instructions; entry checks 39 and frame refcounts).
