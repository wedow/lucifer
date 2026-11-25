---
id: luc-o80b
status: open
deps: []
links: []
created: 2026-11-25T12:59:52Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-ia92
---
# one entry protocol into compiled code

Item 5 of luc-ia92. A unit is entered through four gates and its result handled in four places, with the activation record linked in four and a frame ended after a compiled return in five; the copies have drifted. The design below makes the gate the first thing a unit does (poll word equals the unit's build version; C stack room; the serial pair), moves the activation and the frame end into the unit, and leaves one result handler. By reading it removes about 29 of the roughly 280 instructions of a direct call and about 100 emitted instructions per call site; no whole-program percentage is promised. It also names eleven probable defects found by reading (B1 to B11), none run: the largest is that after any tool (settrace, monitoring) has been on once, user code is never entered compiled again (B1) and kernel units are entered and left at their first poll (B4). Stage 0 writes the tests that show which are real, before any stage that fixes them. Each stage gets a read-only review; stages 1, 3 and 4 (version ordering, activation and reclaim, frame lifetime) an adversarial one. Order against lt-98e0 and the ledger branch: see question 11.


## Design (2026-11-25)


Read from origin/master at 95598ad9. All file:line references are to that tree; the survey in `.tickets/luc-ia92.md` cites 118edfe8, so its line numbers are shifted, and it files this work as its section 2 ("Entry into compiled code"). Nothing was built or run. Every "bug" below is an inference from reading and needs the test named for it. Instruction counts marked "read" are counted from the emitter's sequences; counts marked "estimate" are C code I could not count.

## 0. Summary

- **Today.** A unit is entered through three gates (`lu_jit_enter`, `lu_jit_run`, the emitted `direct_entry`), plus a fourth partial one in every unit's body (`OP_GUARD_ENTRY`, `OP_GUARD_DEPTH`) and about fourteen partial copies in helpers. Its result is handled in four places. The activation record is linked and unlinked in four places. A frame is ended after a compiled return in five.
- **The protocol.** The gate becomes the first thing a unit does, at both of its entry points, emitted by one function. It has three terms:
  1. the carrier's poll word equals the version the unit was built under;
  2. the C stack is above the floor;
  3. for a serial unit, the unlocked word is set and the unit is not dead.
- **What the version compare absorbs.** The version half of the poll word already changes under stop-the-world for global tool changes. Local events and the PEP 523 hook join it, so the compare also stands for `_co_monitoring` and the eval-frame hook.
- **Activation and frame end move into the unit.** The unit links and unlinks its own activation record, and ends its own frame on RETURN and RAISED. Handlers then have frame work only for YIELD (suspend) and for DEOPT/ERROR (the interpreter continues).
- **Cost.** By my count this removes about 29 of the roughly 280 instructions of a direct call (about 10%), about 100 emitted instructions per `H_call` site, and an estimated 20 to 25 per C entry. The large items in `.tickets/lt-98e0.md` (frame references, holds, datastack frame) are not removed here. They end up with one stub and one return sequence to change, instead of the "bound variant of `direct_entry` and `direct_done`" lt-98e0 S1 would otherwise add.
- **Found while reading (inference, not run).** Eleven drifts, section 1.7. The largest: after any tool has been on once, user code is never entered in compiled form again (B1), and kernel units become zombies that are entered and left at their first poll (B4).

## 1. Inventory

### 1.1 Where a unit is entered

| # | Site | Caller | Who made the frame |
|---|---|---|---|
| E1 | `lu_jit_enter`, `runtime/jit/jit.c:1541-1604`, from `jit_enter:` at `runtime/interp/ceval.c:3079-3103` | RESUME (`ceval.c:1548-1551`: a call, or a generator after its own yield) and JUMP_BACKWARD (`ceval.c:3073-3074`) | The interpreter: pushed, linked, current, depth taken. |
| E2 | `lu_jit_run`, `jit.c:1655-1746`, from `lu_eval` (`ceval.c:4441`) | Any C caller with a frame running below: `function_vectorcall` (`runtime/object/function.c:121-126`), `vectorcall_frame` (`function.c:493-497`, the generator send primitive), `PyEval_EvalCode*`, and the helpers `call_function` (`runtime/jit/helpers.c:961`) and `run_resumed` (`helpers.c:779-785`) | C: pushed and bound, not linked. `lu_jit_run` links it above a C-entry marker of its own (`jit.c:1702-1705`). |
| E3 | `direct_entry`, `runtime/jit/x86_64/emit.c:5357-5544`, called in line from `direct_call` (`emit.c:1975-2007`) | A compiled caller at an `H_call` or `H_call_any` site (`call_direct`, `emit.c:2013-2050`, emitted twice when self may be a bound method's) | The stub: pushes, fills and binds in machine code (a copy of `lu_frame_push_exact`, `runtime/interp/frame.h:333-352`). |
| E4 | The same stub through `lu_jit_direct`, `jit.c:1748-1782` | `call_function` (`helpers.c:932`) and `function_vectorcall` (`function.c:100`) | The stub. |
| E5 | The unit's own body: `OP_GUARD_DEPTH` and `OP_GUARD_ENTRY` (`runtime/jit/eval_flow.c:2201-2209`; `emit.c:4380-4398`) | Every entry, after E1 to E4 | |
| E6 | In-line adoption of an existing generator (`runtime/jit/eval_gen.c:516-535`): three guards mirroring `lu_gen_at` (`runtime/interp/gen.c:136-145`) | Inside a consumer's unit | No entry: governed by the consumer's own gate. Listed because its compile-time tools test (`eval_gen.c:662`) is another copy. |

Partial gates in front of these:
- `declines` (`helpers.c:975-979`); `h_send` (`helpers.c:1020`); `h_prim_call` (`helpers.c:1189`).
- Eight tests of `eval_frame` in helpers (`helpers.c:804, 819, 824, 1020, 1124, 1147, 1318, 1347`).
- `call_function`'s `_co_monitoring`/tracing test before `lu_jit_new_gen` (`helpers.c:958`); `raised_arm_ok` (`helpers.c:224-235`).
- `function_vectorcall` (`function.c:95`); `lu_jit_direct`'s pending-exception test (`jit.c:1757`).
- `lu_jit_call_hot` and `lu_jit_backedge_hot` (`runtime/jit/jit.h:88-119`).

### 1.2 What each gate checks, in order

Numbers give the order within each site; "—" means absent.

| Condition | E1 `lu_jit_enter` | E2 `lu_jit_run` | E3 `direct_entry` | E5 body |
|---|---|---|---|---|
| Generator resume kind | In the RESUME handler (`ceval.c:1548`) | 1 (`jit.c:1670-1677`): fresh start at POP_TOP, RESUME 0; after a yield; never inside a delegation | n/a (`lu_jit_direct_nargs` is −1) | |
| `frame->locals == NULL` | 1 (1547) | 3 (1679) | Stored as 0 (5443) | |
| `code->_co_monitoring == NULL` | 2 | 7 (1681) | 6 (5387-5389) | |
| C stack above `jit_stack_floor` | 3, through a thread-local lookup though the task is at hand (`jit.h:70-75`) | 4 | 4 (5379-5381) | |
| Events pending | Not tested: the word is masked (1550) | 5: any event declines (1680) | 5: any event declines (whole-word compare, 5383-5386) | Polls: QSBR stays, visible events stay in kernel code (`emit.c:5130`) |
| Tools version | 6: the code's current version against the word (1549) | 9: the same (1682) | 5: the unit's build-time version, an immediate (5384) | Polls: build-time version (5129, 2226) |
| `ts->tracing == 0` | 4 | 8 | 7 (5390-5391) | |
| No eval-frame hook but perf's | 5 (1548) | — | 8 (5392-5399) | |
| `jc->wait[pc] == 0` | 7, decrements (1557-1566) | — ("a unit is live only while its entry does not wait", 1685) | — | |
| `jc->disabled` | 8 | 6 | — | |
| Unit for (pc, globals, builtins), not dead | 9 `find` (1399-1409) | 10 `find` | 2 dead (5369), 3 globals and builtins (5371-5378) | `GUARD_ENTRY` compares globals and builtins again |
| Compile if none | 10 `compile_at` | — | — | |
| Serial store, then dead | 11 `serial_entry_dead` (1517-1531) | 11 | 1 (5364-5368) | |
| Recursion depth | Taken by the interpreter's push | Taken by the C push | 9 room (5402-5408), then take (5420-5424) | `GUARD_DEPTH` for inlined frames |
| Datastack room | — | — | 10 (5410-5416): declines at a chunk's end | |
| Quiescent report | RESUME's POLL did it | `jit.c:1715` | — | Poll slow path |
| Pending exception | — | `lu_eval` sets it aside (`ceval.c:4438-4439`) | `lu_jit_direct` refuses (1757); the emitted site has none | |

### 1.3 Where a unit's result is handled

| Result | R1 interpreter (`ceval.c:3082-3103`) | R2 `lu_jit_run` (`jit.c:1719-1745`) | R3 `lu_jit_direct` (`jit.c:1767-1779`) with `lu_jit_call_finish` (`helpers.c:993-1010`) | R4 emitted `direct_done` (`emit.c:1902-1967`) |
|---|---|---|---|---|
| NONE / declined | DISPATCH | Returns 0; `lu_eval` interprets | Returns 0; the helper pushes a frame and calls `lu_eval`, whose `lu_jit_run` gates a second time | `test eax`: slow path to the helper, which tries the stub again, then `lu_jit_run`: up to three gate evaluations |
| RETURN, plain frame | `return_frame` (2224-2255): caller current, `lu_frame_drop` | `lu_frame_pop`; current = marker's previous | `lu_frame_pop` | In machine code: `lu_frame_retire` if a frame object, depth leave, DECREF func, DECREF code, `datastack_top = frame` |
| RETURN, generator | `lu_gen_finish`; MISSING for FOR_ITER/NEXT | The same again in C (1727-1736) | n/a | n/a |
| RAISED | `unwound` (4404-4420): pop, then error in the caller | `lu_frame_pop`, NULL. The branch would pop a generator's frame; unreachable only because `leaves_root` refuses generator roots (`runtime/jit/eval_exc.c:211-221`) | `lu_frame_pop`, NULL | Through `lu_jit_call_finish` |
| YIELD | `yielded` (2277-2300): `lu_gen_suspend`, delegation pass-up | `lu_gen_suspend`; current = marker's previous | No case (unreachable: generators have no direct entry) | The same |
| DEOPT | Continues in this loop from `ts->current_frame` | `lu_eval_continue(0)` behind its own marker; does not pin (its callers do) | `lu_jit_call_finish`: inserts a marker under the callee after the fact, pins, `lu_eval_continue`, then `polled` | The same C function |
| ERROR | `goto error` | `lu_eval_continue(1)` | The same with the error flag | The same |
| Activation end | `serial_exit`, unlink, `left` (1585-1587) | The same (1719-1721; again at 1707-1709) | The same from the caller's record (1767-1769) | Emitted: unlink, serial restore, stale test and `lu_jit_left` (1906-1920) |
| Counter | `stats.returns` on RETURN | — | — | — |

### 1.4 Frames: who makes, owns and ends them

| Point | Owner | Code |
|---|---|---|
| Push | Interpreter (`lu_frame_push_exact` at `ceval.c:1869`, `lu_frame_call`); C (`lu_frame_push`, `runtime/interp/frame.c:115-135`); the stub (`emit.c:5418-5457`, twelve field stores and two INCREFs) | Three writers of the frame layout; the stub's is a hand copy of `lu_frame_fill` (`frame.h:311-327`). |
| Unit prologue (`emit.c:5308-5327`) | Compiled code: `stackpointer = localsplus`, `home = owner`, `owner = LU_FRAME_JIT`. The slots hold nothing until an exit or a walker fills them. | |
| During a published call | Still compiled code's. A walker may make it real (`lu_jit_sync`, `runtime/jit/exit.c:425-439`), which sets the owner back; the unit leaves at its next `GUARD_FRAMES`. | |
| YIELD (`emit.c:5230-5248`) | The unit writes the slots, instr, last, stackpointer and owner. The handler suspends the generator. | |
| RETURN / RAISED | The unit unlinks (`OP_UNLINK`, 5205-5228), releases its values and restores the owner. The frame is "still pushed" (`jit.h:24-34`) and the handler ends it. | Depth given back in `OP_UNLINK` for generator roots only. |
| DEOPT / ERROR (`lu_jit_deopt`, `exit.c:658-745`) | The interpreter's again: slots filled, inlined frames pushed above, `ts->current_frame` the innermost. | |

Sites that end or pop a frame on these paths:
- `ceval.c:2239` (return), `4407/4410` (unwound), `4431` (C recursion refusal);
- `jit.c:1651` (`lu_jit_new_gen`), `1738`, `1771`;
- `helpers.c:998`;
- `emit.c:1925-1953`.

The survey's other release sites (`frame_dealloc`, `gen_clear`, the forced-record cleanup at `exit.c:726-743`, the three evaluator drop loops) are its item 8 and are not touched here.

C-entry markers are made in three places: `eval_frame` (`ceval.c:1319-1332`), `lu_jit_run` (`jit.c:1702-1705`), and `lu_jit_call_finish` (`helpers.c:1002-1004`, which inserts the marker under a frame that is already linked).

### 1.5 Activation record, serial words, counters

- **Record.** `struct lu_jit_act` (`runtime/jit/target.h:291-295`) is linked from `task->jit_act`.
  - It is a C local in E1 and E2. For E3 and E4 it sits in the caller's `struct lu_jit_call` (`target.h:341-344`; `CALL_SLOT`, `emit.c:53-57`), written by the callee's stub (5427-5433) and unlinked by the caller.
  - `lu_jit_reclaim` (`jit.c:1004-1043`) walks these lists with the world stopped. Its safety argument is that nothing between taking the unit and linking polls or detaches (`jit.c:972-978`).
- **Serial.** `serial_entry_dead` / `serial_exit` (`jit.c:1517-1540`) are written once in C and again in machine code (stub 5364-5368 and 5535-5538; site 1909-1914).
- **Stale.** `left` (`jit.c:1076-1083`) is called from four places. It must run outside the unit, because it may free the unit.
- **Entry counts.** `bump_entries` (`jit.c:283-290`) runs in C for E1 and E2. The stub increments `u->entries` in line (5516-5522) but not `u->entered`.
- **Kind counters.** `stats.entries` (E1), `stats.direct` or `stats.resumes_compiled` (E2), `stats.compiled` (E3 and E4, `lock inc`, 5524-5531), `stats.returns` (E1 only).
- **Feedback stores on the way in.** The `co_executors` call count (`jit.h:106-119`), the JUMP_BACKWARD cache word (`jit.h:88-100`), `jc->wait[pc]` (`jit.c:1560`), `func->lu_jit_calls` (`own_hot`, `jit.c:1434`), `u->entries`.

### 1.6 Generators

| Case | Path |
|---|---|
| Call of a generator function from compiled code | No unit entry. The helper pushes a frame, `lu_jit_new_gen` runs the prologue and RETURN_GENERATOR in C and pops (`jit.c:1634-1653`), or the generator stays virtual (`eval_gen.c:49-66`). |
| Fresh start | Interpreter: RESUME 0 enters E1. C: `lu_jit_run` recognises POP_TOP then RESUME 0, drops the sent value itself (1698-1700) and enters the call's unit (commit 01a9cae0). |
| Resume after its own yield | E1 from RESUME (oparg location 1), or E2 from `h_for_iter`, `h_send` or the send primitive, through `lu_gen_resume`, then `lu_eval`. |
| Resume inside a delegation (RESUME location 2 or 3) | Never an entry: the interpreter's loop switches frames (`ceval.c:1543-1546`: 121 instructions a level against 429 for a nested entry and a yield). Compiled delegation runs the receiver nested from `H_send` in the root frame only (`eval_gen.c:843-856`); the delegating frame's own suspensions are written as the interpreter's. |
| Throw and close | Never an entry: the throw skips RESUME (`ceval.c:2168-2181`, `EVAL_THROW`), and `declines` hands `gen_throw` to the interpreter. |
| Adoption in line | E6. |

### 1.7 Differences that look like bugs (all inference, not run)

| # | Difference | Test that would show it |
|---|---|---|
| B1 | After any tools version change, user code is never entered again. `force_instrument` allocates `_co_monitoring` for every non-kernel code at its next RESUME (`runtime/interp/instrument.c:1051-1071, 1163-1171`) and nothing frees it before the code dies (1418-1430). All three gates refuse a non-NULL `_co_monitoring` (`jit.c:1547, 1681`; `emit.c:5387`). `sys.settrace(f); sys.settrace(None)` once turns the tier off for user code. | New jitcheck lane `toolsoff` over a new `tests/kernel/jit_tools_toggle.py`: after one on/off toggle, entries and compiled calls must grow with the iterations. |
| B4 | After a version change, kernel units are zombies. `find` does not know the version and the C gates compare the code's current version, so the unit is entered. Its polls compare the build-time immediate and leave at once. A poll exit is no loss (`jit.c:2046`), so nothing kills it. | The same lane: poll exits must stay flat after the toggle. |
| B2 | Local events on code that is inlined, or whose loop is running compiled, are missed. `lu_monitoring_set_local_events` changes no version (`instrument.c:1343-1366`). The only checks are the root's `_co_monitoring` at entry and a compile-time test for inlining (`runtime/jit/eval_call.c:248`). | New `tests/kernel/jit_local_events_inlined.py`: `set_local_events(LINE)` on a callee already inlined in a hot caller, and on the code of the running loop from a callee. |
| B3 | A PEP 523 hook set mid-run is not seen. The setter is a plain store (`runtime/thread/task.c:593-597`). Inlined calls never read it. `lu_jit_run` has no hook test and is reached under a hook for kernel-hidden frames (`frame.h:473-477`), whose units may hold inlined user frames. | New `tests/capi` case `eval_hook_midrun`: a counting hook installed from inside a hot loop and from a callee of `sorted(key=...)`. |
| B5 | A direct call reads `func->func_code`, its JIT state and its entry with no reference (`emit.c:1979-1989`; `jit.c:1752-1755`); the stub's INCREF comes after its checks (5436). `func_set_code` drops the old code at once (`function.c:644-651`) and code dealloc frees the state and units at once (`jit.c:2522-2553`). The interpreter's CALL uses `lu_try_incref_compare` (`ceval.c:1862-1868`). `tests/kernel/threads_call_code_race.py` keeps both codes alive, so it cannot show this. | New `tests/kernel/threads_call_code_fresh.py`: each flip installs `code.replace()`, so the replaced code's last reference is the function's; jit and handoff lanes. |
| B6 | Every compiled return of a plain frame gives its depth back after the unit released the locals, not only `direct_done`'s. The depth leave is in `lu_frame_drop` or `direct_done`, both after `OP_RETURN`; only generator roots give it back in `OP_UNLINK` (5216-5222). The case in `tests/kernel/return_release.py` unwinds by exception and does not reach a compiled normal return. | Extend `return_release.py`: a local with `__del__` measuring the recursion room at a normal return near the limit; jit lane. |
| B7 | `lu_jit_enter` tests `wait[pc]` before `find`. After one function's unit dies, another function of the same code (other globals) with a live unit is interpreted and pays down the first one's wait. Speed only. | jitcheck lane `sharedcode` extended: the second function's executed polls keep growing after the first's unit is killed. |
| B8 | With a visible event deferred inside a kernel builtin (`runtime/interp/poll.c:66`), E1 still enters kernel units, but E2 and E3 decline every call until the builtin returns. Speed only. | A jitcheck lane counting direct calls made by `sorted(big, key=f)` with a signal raised at its start. |
| B9 | `u->entered` misses direct entries (statistics only). | `-X lucifer_jit_stats` report of `tests/kernel/jit_compiled_calls.py`. |
| B10 | A direct entry that declines is tried up to three times (site, `lu_jit_direct`, `lu_jit_run`). Speed only. | Instruction count of a call at a datastack chunk boundary. |
| B11 | Frames handed back by a DEOPT are not brought up to the tools version. The stop-the-world pass instruments code on the raw frame chain (`instrument.c:1277-1292`), which does not hold inlined frames. A loop already running inside an inlined callee goes on in the interpreter uninstrumented until that activation ends. | New `tests/kernel/trace_midloop_inlined_loop.py`: tracing switched on from another thread while the hot loop is in an inlined callee. |

### 1.8 Where the code contradicts the survey

- **Gate count.** The survey counts three gates. The code has a fourth in every unit's body: `GUARD_ENTRY` compares globals and builtins a second time on every path (third time is `find` or the stub).
- **Depth order.** The survey says `direct_done` gives the depth back late while "the interpreter gives it back before". By the code, all compiled plain returns are late (B6); the disagreement is compiled against interpreted.
- **`lu_jit_run`.** The survey says it "adds poll-events and disabled". It also lacks the hook test, adds a quiescent report, and declines on any event where `lu_jit_enter` masks them.
- **`lu_jit_resume`.** `docs/design/jit-architecture.md` §5 lists it for deletion; it no longer exists. Generator resume is `lu_jit_run`'s branch.

## 2. The protocol

### 2.1 The unit's contract

A unit has two entry points into one body, as today:
- `fn(ts, frame, result)` for a frame that exists;
- `entry(ts, func, args, result, takes)` for a call that has only arguments.

At either entry the unit runs the gate before it touches anything, and returns 0 (NONE) with nothing done when it fails. Past the gate it links its own activation, counts the entry, and takes the frame. On the way out it unlinks its own activation.

| Result | State of the world when the unit returns |
|---|---|
| NONE | Nothing was done. |
| RETURN, RAISED | The frame is ended: unlinked, depth given back, values released, then function, code and datastack words given back (a generator: `lu_gen_finish`). `*result` is the value or NULL. |
| YIELD | The frame is saved as YIELD_VALUE saves it and is still to be suspended. |
| DEOPT, ERROR | The frames are the interpreter's, `ts->current_frame` the innermost. `*result` names the root frame. |

### 2.2 The gate

One emitter function, called at both entry points, in this order:

1. **Serial units only:** save the task's unlocked word in the machine frame, store 1, read `dead` (the existing Dekker pair). A dead unit restores the word and declines.
2. **Poll word.** `carrier->poll == V`, where V is the tools version the unit was built under (an immediate, as today). A kernel unit masks the visible events first, as its polls do (`emit.c:5130`). One load of the carrier and one compare for a user unit.
3. **C stack.** `rsp >= carrier->jit_stack_floor`, on the same carrier load.

The stub then does what belongs to making a frame, not to the gate:
- compare the function's globals and builtins with the unit's (the entry word is per code);
- depth room and datastack room, exactly where `lu_frame_push_exact` returns NULL;
- push, fill, bind, cells.

For term 2 to carry tools and hooks, the version must move whenever "the interpreter must see every frame entry" changes:
- **Global events, tool clear, restart:** already do (`instrument.c:1341, 1390, 1411`), with the world stopped, rewriting every carrier's word (606-624).
- **Local events:** `lu_monitoring_set_local_events` bumps it too (its caller already stops the world, `runtime/interp/monitoring.c:250-252`). Fixes B2.
- **The PEP 523 hook:** `_PyInterpreterState_SetEvalFrameFunc` stops the world and bumps it when hook-ness changes (NULL or perf's on one side, anything else on the other). Fixes B3 for running units, at their next poll.

A unit whose version is no longer the interpreter's can never pass. The C wrapper that sees NONE from a live unit with no event pending and `u->version` different from the word kills it (fixes B4), and `compile_at` builds again.

The compile-side predicate, once per build, is one function, `lu_jit_may_compile`:
- the code's version is current;
- no active monitors on the root or any inlined code (replacing the `_co_monitoring != NULL` tests at `jit.c:1547`, `eval_call.c:248`, `eval_gen.c:662`, `helpers.c:231, 958`; fixes B1);
- no hook but perf's;
- `CO_OPTIMIZED`, booted, listed.

### 2.3 What is decided when

| Decided | Facts |
|---|---|
| Once per code | `CO_OPTIMIZED`; direct eligibility and nargs; cells or free variables in the prologue; kernel or user depth accounting; `disabled`. |
| Once per unit (build, then death) | Globals and builtins; the tools version and "no active monitors on any code in it"; no hook; serial or not; every dependency in `runtime/jit/deps.c`. |
| Once per thread poll (the poll word) | Pending events; the tools version (tools, local events, hook); the quiescent-report request; a dead unit's signal (`LU_POLL_DEOPT_SELF`, `deps.c:168-173`). |
| Per entry | C stack room; recursion depth; datastack room; the function's globals and builtins at a dynamic site; `frame->locals` and the resume point for a frame that already exists; `ts->tracing` for an entry from the interpreter or C; a pending exception for a call from C; arguments and `takes`. |

### 2.4 What a direct unit-to-unit call may skip, and why

- **The dead test, for a unit that is not serial.**
  - A kill by this thread happens before the caller loads the entry word, which `kill` has set to NULL (`jit.c:1317-1322`).
  - A kill by another thread that lands after the load is the case "already inside when the fact was taken back". The existing contract covers it: the change reaches a running unit at its next poll (`deps.c:9-13`), and the fact pass assumes only "alive at entry or at the last check" (`runtime/jit/facts.c:300-340`).
  - Memory: a unit is freed only with the world stopped and no activation in it, and nothing between loading the entry word and the prologue's link polls or detaches.
  - Serial units keep the test: their unlocked stores are unsafe from the first instruction once a second thread exists.
- **`_co_monitoring` and the hook.** Both are the poll-word compare. A thread that turns tools on stops the world first; the caller is then at a poll or in a published call, never between its entry-word load and the callee's gate. When it runs again the callee's gate, or the caller's own check after a parked call (`emit.c:2218-2230`), reads the new word.
- **`ts->tracing`.** It is nonzero only inside a tool callback (`instrument.c:577-579, 825-828`), and each increment is undone before control returns. Compiled code calls none of those paths (`raised_arm_ok` keeps units out of the reporting ones). So a unit entered with it at 0 has it at 0 at every call it makes. Stage 1 turns this claim into a fatal check.
- **The quiescent report.** A moved write sequence raises `LU_POLL_QSBR` in every attached carrier's word (`runtime/thread/qsbr.c:60-76`), which fails term 2; the interpreter's RESUME then reports.
- **The second globals compare (`GUARD_ENTRY`).** `find` or the stub has already made it.

Not skippable:
- **The poll-word compare.** It is the callee's RESUME: both its version check and its poll. Loop-free recursion polls nowhere else (`tests/kernel/signals_deep_recursion.py`).
- **C stack room.** Each direct entry nests a machine frame.
- **Recursion depth.** It fixes the RecursionError point.

All three read only this thread's state.

### 2.5 The result handler

One C function, `lu_jit_finish(ts, r, result, marker)`, for every caller that is not the eval loop:
- **RETURN, RAISED:** nothing for the frame. A generator's RETURN with `ret` FOR_ITER or NEXT becomes MISSING: one shared inline, also used by `return_frame`.
- **YIELD:** `lu_gen_suspend`.
- **DEOPT, ERROR:**
  1. Bring each handed-back frame's code up to the tools version (`lu_instrument`, as RESUME would; fixes B11).
  2. Link the marker under the root frame if the caller has none.
  3. Pin and run `lu_eval_continue`.
- Then unlink the marker.

The eval loop keeps its switch, which maps the same kinds onto its own labels:
- RETURN to a new `returned:` label after `return_frame`'s drop;
- RAISED to the tail of `unwound`;
- YIELD to `yielded`;
- DEOPT and ERROR as now, plus the same bring-up-to-date call.

It must read `frame->previous`, `frame->ret` and the kernel flag before the call, since the frame may be gone after.

Inside the unit:
- **Depth.** `OP_UNLINK` gives the depth back for every root, as it does for generator roots today (fixes B6).
- **Fast path.** The return sequence ends the frame in the common case: no frame object, function and code counts above one and owned by this thread, frame not at a chunk's start. It does the two decrements and stores `datastack_top`.
- **Tail.** Anything else leaves by a tail jump, after the machine frame is popped, to a C function that runs the rest of `lu_frame_drop` (or `lu_gen_finish`) and, for a stale unit, `left`. The jump is needed because the code's last reference frees the unit (`emit.c:1941-1943` says so of today's placement), and `left` may free it too.

### 2.6 Thin wrappers, and what each still does itself

| Caller | Keeps |
|---|---|
| `jit_enter:` in `ceval.c` | Saves the frame, reads caller/ret/kernel flag, calls `lu_jit_enter`, maps the result to its labels, delivers the value by `ret`. |
| `lu_jit_enter` | `frame->locals` and `ts->tracing`; pc from `frame->instr`; `find`, then (only when no unit) wait and `compile_at`; `fn`; the stale-version kill; `stats.entries` and `stats.returns`. |
| `lu_jit_run` | The same two tests; the generator resume-point decode and the fresh start's POP_TOP; the prologue's cells, moved before `find`; the C-entry marker; `fn`; `lu_jit_finish`. A NONE after the marker is linked continues by `lu_eval_continue`. |
| `lu_jit_direct` | Pending-exception refusal, nargs, the stub, `lu_jit_finish` for anything but RETURN, the `takes` give-back. |
| Emitted call site | Function or bound-method test, entry-word load, the call, `cmp eax, RETURN`, the value load, `takes_back`; anything else to `lu_jit_finish`. |
| `call_function`, `run_resumed`, `function_vectorcall` | Argument binding and the frame push. They call `lu_jit_run` directly and fall back to `lu_eval` only when it declines. The slow path of a declined emitted call does not try the stub again (B10). |
| `declines`, `h_prim_call`, `h_send` | Their stack test stays: it guards C recursion through any callable, not unit entry. It reads the floor through the task they already have. |

### 2.7 What goes away

- The gate bodies in `lu_jit_enter` and `lu_jit_run`.
- `OP_GUARD_ENTRY` (IR kind, emitter case, two mentions in `opt.c`, one in `ssa.c`).
- The stub's dead, `_co_monitoring`, tracing and hook tests.
- `serial_entry_dead` and `serial_exit`; the activation code in three C functions; the site's unlink.
- `struct lu_jit_call` and `CALL_SLOT`; `lu_jit_left` as a call from machine code; `bump_entries`' C callers.
- `direct_done`'s frame teardown; the frame pops in `lu_jit_run`, `lu_jit_direct` and `lu_jit_call_finish`.
- `lu_jit_run`'s quiescent report and `disabled` test.
- `lu_jit_call_finish` as a separate function.

## 3. Cost

### 3.1 A direct call today

`.tickets/luc-ne54.md` measured about 280 per call: caller about 150, stub about 109 (107 to 136 per execution), callee prologue 7, return 17. Reading the emitter for a user callee that is not serial, called as a plain function with two arguments (one taken), no cells, statistics off:

| Piece | Instructions (read) | Where |
|---|---|---|
| Publish | 2 | `emit.c:1798-1832` |
| Holds | About 10 before and 13 after per held object, plus 6 | 1819-1830, 1846-1873 |
| Argument words | 2 each, 4 words | 2068-2072 |
| Type test | 4 (13 through a bound method object) | 2013-2050 |
| Dispatch: code, state tag, nargs, entry, moves, call, declined test | 20 | 1975-2007 |
| Stub: saves 8, task 1 | 9 | 5362-5363 |
| Stub: dead 3, globals and builtins 8, stack 3, poll 4, monitoring 3, tracing 2, hook 4, depth 2, datastack 7 | 36 | 5369-5416 |
| Stub: push 4, result and activation 7 | 11 | 5418-5433 |
| Stub: fields 16, INCREF of code and function 18 | 34 | 5434-5457 |
| Stub: parameters, 4 taken or 13 counted | 17 | 5458-5471 |
| Stub: current frame 1, entries 3, jump 1 | 5 | 5515-5533 |
| Callee: prologue 7, `GUARD_ENTRY` 8 | 15 | 5317-5326, 4387-4398 |
| Callee: `GUARD_FRAMES` at return 5, unlink 2, return 7, epilogue 8 | 22 | 4399-4430, 5205-5257, 5331-5344 |
| Site after: activation 2, unit 1, serial 2, stale 2 | 7 | 1906-1920 |
| Site after: RETURN compare 2, frame object 3, depth 6, function 14, code 14, datastack 2, take back 10, value and jump 2 | 53 | 1921-1955 |
| Unpublish, result test, frames check | 6 | 1835-1843, 2119-2129 |

Total: 251 plus holds, which is about 275 to 300 with one or two held objects. The two parameter releases in the callee (12 each) are classed as reference counting in luc-ne54 and left out here.

### 3.2 What the protocol removes from it

| Change | Instructions (read) |
|---|---|
| Stub: dead, monitoring, tracing, hook | −12 |
| Body: `GUARD_ENTRY` | −8 |
| Activation in the unit: site −7, stub link 7 becomes prologue 6, frame pointer store −1, epilogue unlink and stale test +5 | −4 |
| Frame end in the unit: site −39; unit +34 (depth 1, frame-object branch 2, function 13, code 13, chunk test and store 5) | −5 |
| **Total** | **about −29, to about 250** |

Not removed, and where they now live:
- **Stub's frame block:** the two INCREF/DECREF pairs of code and function (42), fields (16), parameters, datastack room (7).
- **Unit's entry and return:** saves, prologue and epilogue (30).
- **Site:** holds, publish, dispatch (24).

These are lt-98e0's subject.

Emitted size: `direct_done` is about 55 instructions and is emitted twice per `H_call` site whose self may be a bound method's. It becomes about 6. That is about 100 instructions less per such site, moved to about 40 once per unit.

### 3.3 Interpreter into a unit

Estimates except the emitted parts.

| Piece | Today | After |
|---|---|---|
| RESUME's hot test, save, call and return of `lu_jit_enter` | About 20 (estimate) | The same |
| Gate | About 20 (estimate; includes a thread-local lookup for the stack test) | Two tests in C, plus 5 emitted (read) |
| State, wait, disabled, `find` | About 15 (estimate) | The same; `find` first |
| Activation, serial, entries, `left` | About 20 in C (estimate) | 6 + 3 + 5 emitted (read) |
| Prologue, `GUARD_ENTRY`, return, epilogue | 7 + 8 + 14 + 8 (read) | 7 + 0 + 14 + 8 |
| Frame end | `lu_frame_drop` in line in `return_frame`, about 35 (estimate) | About 34 emitted (read) |

Net: about −25 of about 150 around the body (estimate).

### 3.4 Generator resume from compiled code

The path is `h_for_iter`, `lu_gen_resume`, `lu_eval`, `lu_jit_run`, the unit to its yield, `lu_gen_suspend`. `ceval.c:1543-1546` records 429 instructions for a nested entry and a yield; reading gives about 220 fixed plus the per-slot loads and stores, so I cannot account for about half of it.

| Removed | Instructions | Kind |
|---|---|---|
| The C gate | About 12 | Estimate |
| The quiescent report | 4 | Estimate |
| `GUARD_ENTRY` | 8 | Read |
| `lu_eval`'s C recursion check and pending-exception handling, once `run_resumed` calls `lu_jit_run` first | About 15 | Estimate |
| **Total** | **About −40** | |

### 3.5 What must be measured, and how

- **Direct call cost.** `tools/bench/ladder --only 'rec|meth|krec'`: the slope between `rec2` and `rec12`, and between `meth2` and `meth12`, in `tools/bench/ladders/call.py` is one direct call. Expect about −12 after stage 2, about −4 more after stage 3, about −5 more after stage 4. If stage 4 does not lower the slope, its fast path is wrong.
- **Generator turn.** The same ladder, cases `small_gen` and `calling_gen`.
- **Ledger constant.** `tools/jitcost`, row `call`, checks `CALL_COST` (`jit.c:197`, 420) against the machine; rerun after stages 2 to 4.
- **Idioms.** `tools/bench/idioms time --only calls,closure,genexp,lambdacall,defaultcall`, instructions per iteration.
- **Whole runs.** `perf stat -e instructions:u`, pinned, two runs per arm, on `-c pass`, pyflakes, AWFY Richards and DeltaBlue, as `.tickets/luc-2r1l.md` describes.
- **C entry path.** The 2.4% share (`lu_jit_run`, `lu_jit_enter`, `find`, `bump_entries`; luc-orn7 note) needs `perf record` by hand over the 14 rows with samples split by function. No script in `tools/` does this.
- **Emitted size.** Code bytes per unit from the `-X lucifer_jit_stats` report on `tests/kernel/jit_compiled_calls.py`.

No whole-program percentage is promised. The pieces this touches are, in luc-ne54's steady-state split, direct dispatch 0.5%, entry stub 0.8% and direct return 0.8% of cycles, of which this removes roughly a quarter, plus the 2.4% C entry path, of which the share removed is unmeasured.

## 4. Invariants, as tests against the oracle

New programs are listed with what they must hold. Each runs in the plain, jit-off, jit and jit-deopt lanes of `tools/kernelcheck`; `threads_` programs also run in the handoff lanes.

1. **Recursion points** (`entry_recursion_points.py`; existing `jit_recursion_depth`, `err_recursion`, `deep_recursion`, `recursion_error_chain`, `c_recursion_limit`).
   - The frame count at RecursionError, by an `f_back` walk, for recursion through each entry kind: interpreted, direct, method, through C (`sorted(key=)`, `__hash__`), nested generators, inlined.
   - The same with `setrecursionlimit` changed mid-run.
   - The kernel's "Stack overflow" message for repr recursion.
   - The traceback of the failing level.
   - The room a finalizer finds at a normal return near the limit (B6).
2. **Tools turned on mid-call, same thread** (`entry_tools_midcall.py`; existing `trace_midloop`, `jit_monitor_handlers`, `jit_direct_calls`, `tests/diff/corpus/raise_inlined_monitored.py`).
   - `settrace`, `setprofile`, `sys.monitoring.set_events` and `set_local_events` from inside a directly entered callee, an inlined callee, a compiled generator turn and a finalizer run at entry.
   - Events begin at the next call's start as on CPython.
   - Tools off again, results unchanged.
   - A trace function that itself calls hot user and kernel functions.
3. **Tools from another thread** (`threads_tools_midcall.py`, `trace_midloop_inlined_loop.py`, existing `trace_opcodes_threads`): the same while the first thread is in loop-free direct recursion, in a loop inside an inlined callee (B11), and in a C-entered callee.
4. **Local events and hook** (`jit_local_events_inlined.py`, B2; `tests/capi` `eval_hook_midrun`, B3; existing `tests/capi/capi_monitoring_run.py`).
5. **Compiled again after tools go** (jitcheck lane `toolsoff`, B1 and B4).
6. **Signals and pending calls** (existing `signals_deep_recursion`, `signals_poll`, `jit_slot_signal`, `signal_in_primitive_calls`, `signals_in_try`; new `entry_pending.py`).
   - `_thread.interrupt_main()` raising at the call's return.
   - A signal arriving while a kernel builtin runs compiled callees.
   - `Py_AddPendingCall` from a `tests/capi` extension during compiled recursion.
   - `PyThreadState_SetAsyncExc` on a thread in direct recursion.
7. **Generators** (existing `jit_gen_first_turn`, `jit_gen_turns`, `jit_gen_resumes`, `jit_gen_yields`, `gen_throw_reentry`, `err_generator_throw`, `gen_return_release_frame`, `gen_return_release_state`; new `entry_gen_close_throw.py`).
   - `close`, `throw` and `send` on a generator suspended by a compiled yield.
   - The same calls on one that is running compiled, from its own body and from another thread ("already executing").
   - A throw down a delegation chain whose members have units.
   - GeneratorExit through a `finally` that yields.
   - `gi_running`, `gi_yieldfrom`, `gi_frame.f_lineno` and `f_locals` between compiled turns.
   - A generator dropped unfinished after compiled turns.
8. **Exceptions and tracebacks** (`entry_tracebacks.py`; existing `jit_root_frames`, `jit_inlined_frames`, `jit_virtual_frames`, `frames`, `err_generator_traceback`, `tests/diff/corpus/exception_traceback_frames.py`).
   - A raise under each entry kind, three levels deep.
   - `tb_lineno`, `tb_lasti`, the `f_back` chain and its identity with `sys._getframe(n)`, `f_lineno` of each caller.
   - A frame object kept past the return, with its `f_locals`.
   - An argument-binding TypeError at a direct site.
9. **Collection and finalizers at entry** (`entry_finalizers.py`; existing `call_args_finalizers`, `jit_release_frames`, `jit_release_order`, `jit_reclaim_frames`, `jit_return_release_kill`, `frame_teardown`; the `gc:1` lane).
   - A collection due at the callee's first poll.
   - A finalizer at entry or return that walks frames, rebinds a global the callee folded, replaces the callee's `__code__`, or raises.
   - The value a dead unit held released no later than today.
10. **Thread handoff** (handoff lanes over all of the above; existing `threads_serial_release`, `jit_reclaim_threads`, `jit_reclaim_fork`; new `threads_kill_under_calls.py` and `threads_call_code_fresh.py`).
    - A thread started inside a directly entered callee.
    - `sys._current_frames()` from another thread across a chain of direct entries.
    - A global the callee folded, rebound by another thread under a calling loop: each call sees old or new, and new after the join.
    - B5.
    - `os.fork` inside a callee.

## 5. Stages

Line counts are estimates. Every stage runs `tools/floor quick`; the gate column adds what to select. Each claimed bug gets its failing test in stage 0, before the stage that fixes it.

| Stage | Change | Files | Lines (+/−) | Gate | Should move |
|---|---|---|---|---|---|
| 0 | The tests of section 4 and the jitcheck lane, each shown failing or passing on master. | `tests/kernel/` (9 programs), `tests/capi/` (1), `tools/jitcheck` | +520 tests | kernelcheck plain, jit-off, jit, jit-deopt, handoff; jitcheck | Nothing. Records which of B1 to B11 are real. |
| 1 | The gate and the handler exist once; every old site calls through or checks against it (details below). | `runtime/jit/jit.c`, `jit.h`, `target.h`, `helpers.c`, `eval_call.c`, `eval_gen.c`, `x86_64/emit.c`; `runtime/interp/instrument.c`, `instrument.h`, `ceval.c`; `runtime/thread/task.c` | +240 / −145 | All kernel lanes; handoff; jitcheck with `toolsoff`; `check-capi-ext`; the `test_monitoring` and `test_sys_settrace` units of the floor | Lane `toolsoff` passes (B1, B4); B2, B3, B7, B11 tests pass. C entry: gate about −12 (estimate). The direct slope rises by the temporary gate, about +5. |
| 2 | Delete what the gate made redundant: the stub's old tests and their fatal branches, `OP_GUARD_ENTRY`, the old-predicate checks in C, `lu_jit_run`'s quiescent report and `disabled` test. | `x86_64/emit.c`, `eval_flow.c`, `opt.c`, `ssa.c`, `ssa.h`, `jit.c` | +10 / −80 | jit and jit-deopt lanes; handoff; jitcheck (`sharedcode`, `compiled`, `ccalls`) | Ladder slope `rec` and `meth`: −20 from stage 1, −12 to −20 from master. `-c pass` and pyflakes instructions slightly down. |
| 3 | The activation in the unit: link, serial pair and entry count in the prologue; unlink in the epilogue; a stale unit leaves by tail jump to C. Cells before `find` in `lu_jit_run`. Delete `struct lu_jit_call`, `CALL_SLOT`, `serial_entry_dead`, `serial_exit`, the activation code in three C functions and at the site, `bump_entries`' callers. `u->entered` counted in one place (B9). | `x86_64/emit.c`, `jit.c`, `target.h`, `helpers.c` | +60 / −95 | All kernel lanes; handoff and `handoff:3`; jitcheck; `jit_reclaim*`, `threads_serial_release`, `jit_release_frames` by name | Slope about −4. Code bytes per `H_call` site down. |
| 4 | The unit ends its frame: depth in `OP_UNLINK` for every root, the return fast path, the C tail. `lu_frame_drop` split so the tail is its second half. Interpreter labels `returned` and the RAISED tail. `direct_done` reduced to the compare and the value. Frame pops deleted from three C sites. Post-condition checks in the C wrappers after RETURN: `current_frame` is the caller, depth and `datastack_top` are as before the push. | `x86_64/emit.c`, `eval_call.c`, `jit.c`, `jit.h`, `helpers.c`; `runtime/interp/ceval.c`, `frame.h` | +100 / −85 | All kernel lanes; handoff; `gc:1`; jitcheck; `return_release`, `jit_return_release_kill`, `jit_release_order`, `frame_teardown` by name | B6 test passes. Slope about −5. Code bytes per site about −100 instructions. `tools/jitcost` row `call` still within its factor. |
| 5 | C callers enter through the core: `run_resumed` and `call_function` call `lu_jit_run` before `lu_eval`; a declined emitted call does not retry the stub (B10). If B5 is real, `func_set_code` releases the replaced code through `_PyObject_XDecRefDelayed`, which closes the window for both direct paths. `docs/design/jit-architecture.md` §11 updated. | `helpers.c`, `jit.c`, `runtime/object/function.c`, `docs/design/jit-architecture.md` | +30 / −40 | jit lanes; handoff; jitcheck `turns`, `ccalls`; `threads_call_code_*` | Ladder `small_gen`, `calling_gen`: about −15 a turn (estimate). |

Totals: about +440 / −445 in C, of which about 60 are stage 1's checks deleted in stage 2, so lines are roughly neutral. The survey's −300 did not count the version handling, the bug fixes or the moved activation.

Mechanism count:

| Mechanism | Today | After |
|---|---|---|
| Gates | 4 | 1 |
| Result handlers | 4 | 1, plus the interpreter's label mapping |
| Activation code | 4 copies | 1 |
| Frame end after a compiled return | 5 sites | 1 fast path and 1 C function |

### Stage 1 in detail

- **Gate.** One emitter function `gate()`, called at both entry points; `u->version` recorded at build.
- **Version sources.** `lu_monitoring_set_local_events` and the hook setter bump the version; stale-version kill in the C wrapper.
- **Compile predicate.** `lu_jit_may_compile` with the active-monitors test replaces the `_co_monitoring` tests.
- **C entry core.** One static `enter()` in `jit.c`, called by `lu_jit_enter` and `lu_jit_run`: the two frame tests, `find` before the wait, `fn`.
- **Handler.** `lu_jit_finish` replaces `lu_jit_run`'s tail, `lu_jit_direct`'s tail and `lu_jit_call_finish`, and brings handed-back frames up to date.

### The stage 1 check

Asserts are in every build, and a race with another thread's kill is legitimate, so the check has named gaps.

- **In C.** `enter()` evaluates today's predicate of its caller before `fn` and compares after.
  - If the unit ran and the old predicate was false, call `Py_FatalError` naming the site and the condition. The one allowed gap is "monitoring data present with no active monitors".
  - If the unit declined and the old predicate was true, count it under `-X lucifer_jit_stats`. Allowed gaps: an event was pending, or the unit's version was stale.
- **In the stub.** The old tests stay behind the new gate with their branch retargeted:
  - `_co_monitoring` non-NULL calls a C check that is fatal unless the active monitors are empty;
  - tracing nonzero is fatal (this is the proof of the inheritance claim in 2.4);
  - a hook other than perf's is fatal;
  - `GUARD_ENTRY` failing is fatal.
- The dead test stays a decline in stage 1 and is deleted in stage 2.

## 6. Risks and open questions

1. **Does anything read `_co_monitoring != NULL` as "tools are on" besides the sites listed in 2.2?** `git grep _co_monitoring -- runtime` lists 46 uses; the five outside `instrument.c`, `legacy_tracing.c` and `monitoring.c` are the ones replaced.
2. **Is `ts->tracing` ever nonzero at a direct call?** Stage 1's fatal branch answers it on the floor's `test_sys_settrace`, `test_monitoring` and `cprofile_stats`.
3. **Does a version bump on `set_local_events` change anything Python can see?** Candidates are `restart_events` and DISABLE bookkeeping at `instrument.c:1176-1184`. The floor's `test_monitoring` unit answers it.
4. **A debugger that sets local events often kills every unit each time.** Measure builds per toggle in a jitcheck lane over `jit_tools_toggle.py` with 100 toggles. If it matters, the alternative is a code dependency kind in `deps.c` registered for the root and each inlined code, with no bump.
5. **Can anything between `find` and the prologue's link detach or poll once the link moves into the unit?** Read `lu_jit_run`: the cells (`PyCell_New`) move before `find`; the debug prints move before `find` or after the return. `jit_reclaim_threads` in the handoff lanes with `gc:1` is the test.
6. **Does a tail jump out of a unit's epilogue break `-X perf` symbolization or unwinding?** `runtime/interp/perf.c` maps unit ranges. Check one `perf report` of `tests/kernel/jit_compiled_calls.py` under `-X perf`.
7. **Is `GUARD_ENTRY` ever taken today?** jitcheck's `sharedcode` lane counts "exits at a unit's entry". If the count is nonzero on master, some path enters a unit with another function's frame, and stage 2 must not delete the guard until it is found.
8. **Does the return fast path cost the interpreter-entered path anything?** It replaces `return_frame`'s in-line drop. Compare `tools/bench/idioms` row `calls`, and fib by `perf stat`, under `-X lucifer_jit=always` before and after stage 4.
9. **Are kernel functions' code and function objects immortal or deferred?** If so the fast path's two decrements are five instructions each, not twelve, and section 3's totals for kernel callees are lower. Read `runtime/object/function.c:150-165` and the image loader.
10. **Should `lu_jit_enter` still decline while an event is pending in user code?** Today it masks events. The new gate declines for one interpreted entry, after which RESUME's poll clears the event. Count the stage 1 "declined, old predicate true, event pending" statistic on pyflakes.
11. **Ordering with `.tickets/lt-98e0.md` and the ledger branch (luc-1kz0).**
    - lt-98e0 S1 as written adds a second stub and a second return handler. Land this first, and S1 becomes "the stub and the return fast path skip the two reference pairs for a flagged frame".
    - The ledger branch edits `kill` and `wait_at` in `jit.c`; stages 1 and 3 touch the same functions.
12. **Is B5 real, and is a delayed release of a replaced `__code__` enough?** `threads_call_code_fresh.py` under the handoff lanes answers the first. For the second: the window from the unreferenced read to the stub's INCREF holds no quiescent report (the gate declines when `LU_POLL_QSBR` is set, before any report), so a delayed release cannot run inside it. That also holds for the interpreter's CALL, which could then drop `lu_try_incref_compare` (0.39% of pyflakes with the tier off, luc-2r1l).

### Files
runtime/jit/x86_64/emit.c, runtime/jit/jit.c, runtime/interp/ceval.c, runtime/jit/helpers.c, runtime/interp/instrument.c.

## Stage 0 results (2026-11-25)

Run on origin/master 7260c37d. Counts from `-X lucifer_jit=always -X lucifer_jit_stats`; lanes by `tools/kernelcheck`.

| # | Verdict | Evidence |
|---|---|---|
| B1 | **Real** | `jit_tools_toggle.py N TOOL`: with no toggle, `entries 7371, ... direct calls 317` at both 20,000 and 40,000. After one settrace, setprofile or monitoring on/off: `entries 67060` at 20,000, `127060` at 40,000 (3 per iteration: the interpreted loops enter the `max`, `sum`, `min` units); no user unit is built after the toggle. `sys.settrace(sys.gettrace())` with nothing on (doctest's restore) does not trigger it. jitcheck `toolsoff`: `GAP toolsoff: settrace: 67614 at 20000 iterations, 127614 at 40000` (same for setprofile, monitoring; restore passes). |
| B4 | Not real (as probed) | Same runs: `poll 134` exits at both sizes; the kernel units entered after the toggle run to their return (`unit sum@1 entries 19897 ... poll 417837`). |
| B2 | **Real** for an inlined callee; not for the running loop | `jit_local_events_inlined.py`: oracle `inlined callee 10`, lucifer `2` (plain, jit), `4` (jit-deopt); jit-off matches. Local events on the code of a running loop, armed from a callee, match in every lane. |
| B3 | Not determined | `_PyEval_EvalFrameDefault` is an unimplemented stub, so a counting hook cannot delegate; no small `tests/capi` case. |
| B5 | Not shown | `threads_call_code_fresh.py` passes plain, jit-off, jit, jit-deopt, handoff, handoff:3 (no crash in any run). |
| B6 | Not shown | `return_release.py` extension (finalizer measuring recursion room at a normal return, mid-stack and at the limit) matches in all four lanes. |
| B7 | Real, bounded, speed only | A probe (two functions of one code, the first's unit killed by a global rebind): the second's unit gets 19,922 of 20,000 calls against 20,013 without the kill; `entries waited` +184. Flat across sizes. |
| B8 | Not shown | `sorted(range(N), key=f)` with `interrupt_main(SIGUSR1)` at the first key call: `compiled calls 61329` against `61332` without (20,000), `101329` against `101332` (40,000). |
| B9 | **Real**, statistics only | `jit_compiled_calls.py 20000`: `unit fib@1 entries 10 ... poll 958 helper 954`: about 950 executions of a loop-free unit, 10 counted entries. |
| B10 | Not determined | No instruction count taken at a datastack chunk boundary. |
| B11 | Not shown | `trace_midloop_inlined_loop.py` (LINE from another thread while the loop runs inside an inlined callee) matches in all four lanes. |

Size of B1. One process, a loop of a call and an instance method per iteration, best of 5: 2.0 ns per iteration before the toggle, 399.6 after (195x; `lucifer_jit=off` runs it at 362 and 350). Whole run, `perf stat` pinned, two runs per arm: 0.295 G instructions without the toggle, 12.63 G with one settrace on/off before the loop (43x). In the vendored stdlib B1 is reached by deliberate tools: pdb/bdb (`settrace`, or `monitoring` with that backend), `profile` (`setprofile`), cProfile (`monitoring.set_events` in `_lsprof.c`), `trace`, `threading.settrace`/`setprofile`, coverage-style tracers. One `cProfile.run` or `breakpoint()` + continue leaves the rest of the process interpreted. Nothing in unittest, asyncio, warnings, importlib or threading turns a hook on implicitly; doctest's unconditional restore is a `settrace(None)` with nothing on, which does not trigger it.

New programs, all diffed against the oracle in the plain, jit-off, jit and jit-deopt lanes (threads_ also handoff, handoff:3): `entry_recursion_points`, `entry_tools_midcall`, `threads_tools_midcall`, `threads_entry_pending`, `entry_gen_close_throw`, `entry_tracebacks`, `entry_finalizers`, `threads_kill_under_calls`, `threads_call_code_fresh`, `jit_local_events_inlined`, `trace_midloop_inlined_loop`, `jit_tools_toggle`; `return_release` extended; jitcheck lane `toolsoff` (in `NOT_BUILT` until stage 1).

Failing on master, kept and marked in kernelcheck's `XFAIL` (the handoff-only expected-fail rule extended to every lane):
- `jit_local_events_inlined` (B2, this ticket); XPASS in jit-off.
- `entry_tracebacks` (luc-mj6t): a frame kept past its return reports `f_lineno` of its last sync, not the return line; plain, jit-off, jit; XPASS in jit-deopt. Not an entry-protocol defect.
- `threads_kill_under_calls` (luc-33lu): `sys._current_frames()` has no entry for a thread blocked in `Event.wait()`; every lane. Not an entry-protocol defect.

No `tests/capi` `eval_hook_midrun` case (B3 above).

## B1 fix ahead of stage 1 (2026-11-25, branch luc-tools, not landed)

State: fix and tests committed; kernel gate has one new failure (below); numbers and the remaining gates pending.

- Predicate `lu_code_watched` (instrument.h): monitoring data present and (its `watched` byte, or the code's tools version differs from the interpreter's). `force_instrument` computes the byte as "local events active, or any global event of any tool on", stores 1 before instrumenting any opcode and the final value after the last opcode is restored, then publishes the version (release); the predicate reads the version (acquire) before the byte. Only `lu_monitoring_set_events` writes the global events and it bumps the version in the same stop-the-world (instrument.c, set_events then set_global_version); clear_tool and restart bump it too. Local events change under stop-the-world (sys.monitoring.set_local_events, f_trace_opcodes).
- Pristine bytecode: with the active local monitors empty, `force_instrument` removes every event, line and per-instruction tool (remove_tools / remove_line_tools / remove_per_instruction_tools de-instrument once no tool is left), so no INSTRUMENTED opcode remains and the line and per-instruction tables are read only through those opcodes.
- Sites: `lu_jit_enter`, `lu_jit_run`, `raised_arm_ok`, `call_function`'s `lu_jit_new_gen`, `not_inlinable`, the generator adoption test, and the direct-entry stub (load the monitoring pointer; if non-NULL, compare its `watched` byte).
- Tests: jitcheck `toolsoff` (now gated; variants settrace, setprofile, monitoring, restore, cprofile, breakpoint, local) fails on master (`settrace: 67855 at 20000 iterations, 127855 at 40000`, likewise every tool but restore) and passes with the fix (`8363, 8363` ...). New `tools_toggle_again.py` passes in plain, jit-off, jit, jit-deopt, handoff.
- New failure: `entry_tools_midcall` (passes on master) fails in plain and jit. Its later scenarios ran interpreted on master (B1) and now run compiled. Three scenarios: `monitoring finalizer` (a tool switched on by a finalizer as a compiled callee starts; also fails on master when it is the first toggle, so pre-existing), `local generator sum` (local events on a callee inlined into a generator a kernel consumer runs; also fails on master when first: B2), `setprofile generator sum` in jit (profile events of a generator run by `sum` missed after earlier toggles; passes on master and here when it comes first or second; cause not found).
