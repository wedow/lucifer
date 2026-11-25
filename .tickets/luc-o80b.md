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

State then (superseded by the section after the cause grouping): fix and tests committed; not landable alone. The gate loses `entry_tools_midcall` and 12 test_monitoring floor tests (below): with B1 fixed, code that used to stay interpreted after an earlier toggle runs compiled again and meets the event holes B1 was hiding.

- Predicate `lu_code_watched` (instrument.h): monitoring data present and (its `watched` byte, or the code's tools version differs from the interpreter's). `force_instrument` computes the byte as "local events active, or any global event of any tool on", stores 1 before instrumenting any opcode and the final value after the last opcode is restored, then publishes the version (release); the predicate reads the version (acquire) before the byte. Only `lu_monitoring_set_events` writes the global events and it bumps the version in the same stop-the-world (instrument.c, set_events then set_global_version); clear_tool and restart bump it too. Local events change under stop-the-world (sys.monitoring.set_local_events, f_trace_opcodes).
- Pristine bytecode: with the active local monitors empty, `force_instrument` removes every event, line and per-instruction tool (remove_tools / remove_line_tools / remove_per_instruction_tools de-instrument once no tool is left), so no INSTRUMENTED opcode remains and the line and per-instruction tables are read only through those opcodes.
- Sites: `lu_jit_enter`, `lu_jit_run`, `raised_arm_ok`, `call_function`'s `lu_jit_new_gen`, `not_inlinable`, the generator adoption test, and the direct-entry stub (load the monitoring pointer; if non-NULL, compare its `watched` byte).
- Tests: jitcheck `toolsoff` (now gated; variants settrace, setprofile, monitoring, restore, cprofile, breakpoint, local) fails on master (`settrace: 67855 at 20000 iterations, 127855 at 40000`, likewise every tool but restore) and passes with the fix (`8363, 8363` ...). New `tools_toggle_again.py` passes in plain, jit-off, jit, jit-deopt, handoff.
- New failure: `entry_tools_midcall` (passes on master) fails in plain and jit. Its later scenarios ran interpreted on master (B1) and now run compiled. Three scenarios: `monitoring finalizer` (a tool switched on by a finalizer as a compiled callee starts; also fails on master when it is the first toggle, so pre-existing), `local generator sum` (local events on a callee inlined into a generator a kernel consumer runs; also fails on master when first: B2), `setprofile generator sum` in jit (profile events of a generator run by `sum` missed after earlier toggles; passes on master and here when it comes first or second; cause not found).
- Gates: full jitcheck 0 failing (`toolsoff` PASS, the usual NOT_BUILT gaps); check-capi-ext PASS (`capi_monitoring_run`); the floor units test_monitoring, test_sys_settrace, test_sys_setprofile, test_cprofile in jit mode against master's binary in the same mode: 0 gained, 12 lost, all test_monitoring (MonitoringEventsTest.test_nested_call, SimulateProfileTest.test_balanced, TestBranchAndJumpEvents.test_loop, TestLineAndInstructionEvents.test_c_call/test_simple/test_try_except/test_with_restart, TestLoadSuperAttr.test_attr/test_method_call/test_method_call_error, TestManyEvents.test_simple/test_try_except); deopt mode loses 11 of the same against the off floor.
- Numbers (instructions:u, pinned, two runs per arm, master vs fix): `-c pass` on 132.35M/132.47M (+0.09%), always 849.28M/849.34M (+0.01%); pyflakes on 9.935G/9.955G (+0.2%), always 16.211G/16.213G (+0.01%); Richards on 2.5467G/2.5456G (-0.05%), always 3.3792G/3.3778G (-0.04%). The toggle loop: whole run 12.63G -> 0.298G (no toggle 0.298G); in one process 2.0 ns/iteration before the toggle and 373.5 after on master, 2.0 and 2.9 with the fix.

## B1: what the lost tests miss, by cause (2026-11-25)

Every one of the 12 lost test_monitoring tests passes alone with the B1 binary and fails only after earlier tests of the module have toggled a tool: their helpers (`get_events`, `gather_events`) then run compiled again, which master never did after the first toggle. Four causes, reduced to programs diffed against the oracle (B1 binary, plain and jit unless noted).

- **A. A tool turned on by a call the unit itself makes is not seen until the unit's next poll.** The stop-the-world pass instruments the code of every frame on the chain, the unit's root among them, but the unit returns from its call and runs on: only a call that parked compares the poll word after it (`parked_end`), and the thread that stops the world parks nothing. The frame's own events between the call and the next poll are lost. Reduction: a function whose loop is entered compiled and which then calls `set_events(LINE)` and runs two lines: oracle `[('g_on', 5), ('g_on', 6)]`, lucifer `[]`. All 12 test_monitoring losses are this: each misses events of `get_events`/`gather_events` after its `set_events` (`('line', 'get_events', 10)` first, or test_nested_call's first `py_call` and last `return`, which `events[:-1]` then cuts). `entry_tools_midcall`'s `monitoring finalizer` is this too: `t_finalized`'s line after `box.clear()`, whose finalizer turned the tool on. Master shows it where a code runs compiled before its first instrumentation.
- **B. Frames an exit hands back are not brought up to the tools version (design 2.5, the B11 class).** Inlined frames are not on the chain the pass walks, and the interpreter instruments only at RESUME, so a handed-back inlined frame runs on uninstrumented until it returns. It shows once A makes the unit leave at the call: a tool turned on from an inlined callee.
- **C. Local events change no version (B2, design 2.2).** `local generator sum` (`t_leaf` inlined into the generator `sum` runs) and `jit_local_events_inlined`: a unit with the code inlined goes on running it.
- **D. A unit built under an older tools version is entered.** `lu_jit_enter` and `lu_jit_run` compare the code's version, which RESUME stamps afresh (a kernel code's always), not the unit's; the unit then runs what it inlined or adopted at its build until its first poll. `setprofile generator sum` in jit: `sum`'s kernel unit, built before the toggle with the generator adopted in line and `at` inlined into it, resumes the generator compiled while the profiler is on: its `call` and `return` events are lost. Reduced with `LUCIFER_JIT_ONLY=_sum,at` (fails) against `_sum` or `t_gen` alone (passes); the scenario fails at its second or third repetition in one process.

Fixes, in the design's terms where it has one:
- A: the stop-the-world pass makes every frame compiled code runs real (`lu_jit_sync`, as a walker writing locals does), so the unit leaves at the frame check after its call; that check follows every instruction that may run Python.
- B: the exit brings each handed-back frame's code up to the tools version (`lu_instrument`, as RESUME would).
- C: `lu_monitoring_set_local_events` bumps the version under its stop-the-world and runs the same pass; `lu_code_watched` already refuses inlining a code with local events.
- D: a unit records the version it was built under; the two C gates kill a unit whose version is not the poll word's (the design's stale-version kill, B4).

## B1 with its event holes closed (2026-11-25, branch luc-tools, not landed)

State: B1, the four fixes above and their tests committed; gated as below; awaiting the adversarial review. No test lost against master's binary.

- Tests, each failing before (B1 binary, and a build without the one fix) and passing after, diffed against the oracle: `tools_own_call` (A: a root entered at its loop turns `set_events` on and its own lines, calls and return are missed; B: the same from a callee run in line, missed only without the exit's bring-up), `tools_adopted_gen` (D: `sum`/`list`/`sorted` resuming a generator they took in line after `setprofile` or `set_events` went on from inside it, three times each; also fails without B), `jit_local_events_inlined` (C; leaves kernelcheck's expected-fail list), `entry_tools_midcall` (A, C, D). `threads_tools_own_frame` (another thread turns the tool on while the loop is blocked in a call it made) passes before and after; kept for the handoff lanes.
- Gate: the 15 tool and trace kernel programs plus the three new ones in plain, jit-off, jit, jit-deopt and handoff (18/18 each), the threads ones in handoff:3 (4/4); jitcheck 0 failing (`toolsoff` PASS, the usual NOT_BUILT gaps); check-capi-ext PASS; `tests/diff/corpus/raise_inlined_monitored.py` matches in on, off, jit, jit-deopt; the floor units test_monitoring, test_sys_settrace, test_sys_setprofile, test_cprofile: 0 lost and 0 gained against master's binary in jit mode and in deopt mode (566 pass, 26 fail, 1 error in all four runs). test_monitoring alone in jit mode fails the same five tests as master's binary.
- Numbers (instructions:u, pinned, two runs per arm, master's binary against this branch): `-c pass` on 132.28M/132.48M (+0.15%), always 849.21M/849.54M (+0.04%); pyflakes on 9.973G/9.984G (+0.11%), always 16.222G/16.232G (+0.06%); Richards on 2.5082G/2.5072G (-0.04%), always 3.3338G/3.3327G (-0.04%). The toggle loop: whole run with one settrace toggle 12.63G -> 0.299G (no toggle 0.298G); in one process 2.0 ns/iteration before the toggle and 381.2 after on master, 2.0 and 2.8 here.
- Question 4 (100 local-event toggles on a cold code between runs of a hot loop, `-X lucifer_jit_stats`): master builds nothing per toggle; this branch about 6.7 builds per toggle (compiles 19 -> 693 under `on`, 272 -> 942 under `always`) and 0.7-0.8 ms per toggle. Every hot unit dies at its next entry because the version is global. Two costs follow, neither fixed here: a tool that sets local events per new code (a coverage tool) rebuilds the hot units each time; and each toggle spends a version, of which there are 2^19 before `events set too many times` (CPython's local events spend none). The design's alternative is a code dependency (deps.c) on the root and each inlined code, killing only the units that hold the code; it needs a dependency without a guard and the dead checks it brings.

## Review fixes (2026-11-25, branch luc-tools, not landed)

State: the adversarial review's M1 and M2 fixed and gated; its per-code dependency filed as lt-crb9.

- M1: a unit's polls, parked calls and direct entry now compare the version the unit records at the start of its build (one source); the C gate checks what compile_at returns as well as what find returns, so a build that a tools change landed in dies at its first entry. Not forced by a test (the change must land inside a build); `threads_local_events_inlined` toggles local events on a callee from another thread while the hot loop is compiled again and again (all lanes, handoff:3).
- M2: versions use the whole word (`global_version`, `set_global_version`, the capi scope's version); the four `events set too many times` checks are gone with the limit. `tools_version_many` (600,000 local-event changes) fails before with `OverflowError: events set too many times` (plain, jit) and passes after.
- Low: `lu_code_watched` reports a stale version even when the code has no monitoring data (inlining and generator adoption no longer take a code a tool may watch); kernel and shim code are never watched. Recorded, not fixed: a MemoryError from `lu_instrument` in `lu_jit_deopt` is raised at the innermost frame even when an outer one failed.
- Back-off for a tool that changes on every call: tried raising the code's level on every tools kill (`escalate`) and took it out. The reviewer's loop (LINE on X at its start, off at its return, X called from a hot loop) went from 44 to 233 builds between 2,000 and 8,000 iterations to 21 to 30, but the level never decays for a loop unit (one entry each), so after the question-4 run's toggles the hot loop stayed interpreted: its post-toggle phase took 46 ms (on) and 248 ms (always) against 4.7 ms, and about 25 toggles reach MAX_LEVEL, which is B1 again. lt-crb9 removes the kills instead.
- New tests: `tools_version_many`, `tools_toggle_points` (clear_tool_id, free_tool_id, restart_events part-way through a compiled run, settrace_all_threads, a generator suspended across a toggle, f_trace_opcodes on the outermost frame of a recursion only), `threads_local_events_inlined`; verified in a fresh worktree built with make -j4 in plain, jit-off, jit, jit-deopt, handoff, handoff:3.
- Gate: 21 programs in plain, jit-off, jit, jit-deopt and handoff, 5 in handoff:3, all pass; jitcheck 0 failing (`toolsoff` PASS); check-capi-ext PASS; `raise_inlined_monitored` matches in four modes; the four floor units: 0 lost, 0 gained against master's binary in jit and deopt.
- Numbers (instructions:u, pinned, two runs per arm, master's binary against this branch): `-c pass` on +0.17%, always +0.04%; pyflakes on +0.12% (master's second run read 9.947G against 9.973G: +0.25% on the mean), always +0.12%; Richards on -0.03%, always -0.03%. Toggle loop: 2.0 ns/iteration before and 2.8 after one settrace toggle (master 366). Question 4 unchanged: about 6.7 builds and 0.7-0.8 ms per local-event toggle.

## Stage 1 on master after the tools fix (2026-11-25)

Read from origin/master f0bde3b5 (tools fix d3367bc4 in it).

Already true:
- Version sources: global events, tool clear, restart and local events move the version under a stopped world (local events since the tools fix). The stop-the-world pass makes compiled frames real; `lu_jit_deopt` brings handed-back frames up to the tools version (B11's class).
- `u->version` is recorded at build and every emitted compare (polls, parked calls, the stub) uses it.
- Stale-version kill: `tools_moved` in both C gates (before the entry, not after a decline).
- Compile-side predicate: one function already, `lu_code_watched` (instrument.h), used by both C gates, inlining, generator adoption, `raised_arm_ok`, `call_function`'s new-generator path and (in its emitted form) the stub. No `lu_jit_may_compile` is needed.

Changed shape: the `_co_monitoring != NULL` tests became `lu_code_watched` (a `watched` byte plus the code's version), not an active-monitors walk.

Remaining (this stage): the hook setter moving the version; the gate emitted once at both entry points; `enter()` shared by `lu_jit_enter` and `lu_jit_run` with `find` before the wait (B7); `lu_jit_finish` for the three C tails; the stage 1 checks.

## Stages 1 and 2 (2026-11-25, branch luc-entry1, not landed)

Stage 1 (d2d8edbd):
- `gate()` in the emitter, called by the unit's frame entry (before `entered`) and its direct entry: the serial pair (word kept in the caller's record, or the unit's own call record at frame entry), C stack above the floor, poll word equal to `u->version` (a kernel unit masks the visible events). It clobbers RAX and R11 only, since the stub's RCX holds the result word.
- `unit_for()` (the two frame tests, `find` before the wait, then wait/disabled and `compile_at`) and `enter()` (activation, `fn`, and on a decline the stale-version kill, made before `left` because `left` may free a stale unit) are shared by `lu_jit_enter` and `lu_jit_run`. They are two functions, not one `enter()`: `lu_jit_run` makes the frame ready (marker, the fresh generator's sent value, the cells prologue) between the find and the call until stage 3 moves the cells before the find. A decline after that continues in the interpreter from the RESUME through `lu_jit_finish`. The prologue can collect, so `lu_jit_run` finds the unit again after it.
- `compile_at` refuses a watched code, a hook other than perf's, or a low C stack (the conditions the gate holds while a unit lives). `lu_jit_direct` refuses a call while `ts->tracing` is set: a C caller inside a tool's callback is legitimate there, so the stub's tracing test could not become fatal for C callers.
- `lu_jit_finish(ts, frame, r, value, entry)` replaces `lu_jit_run`'s tail, `lu_jit_direct`'s tail and `lu_jit_call_finish`; the emitted site calls it.
- `_PyInterpreterState_SetEvalFrameFunc` stops the world and moves the version when hook-ness changes (`lu_monitoring_hook_changed`). There is no test: no counting hook can delegate (B3).
- Defect found by the gate: `prologue()` (C) ran past an INSTRUMENTED_RESUME, making cells of bytecode. On master it never ran on instrumented code, because the C gate refused first. Seen as a collector segfault in test_cprofile under `jit` (an evil timer calls a function with a free variable from C while the profiler is on). Fixed (it stops at INSTRUMENTED_RESUME). Test `entry_prologue_tools` fails without the fix in plain and jit and passes after; master passes.
- Checks: none fired. That covers the C old-predicate fatal, the stub's monitoring, tracing and hook fatals, and GUARD_ENTRY fatal, over the gate's kernel programs in plain, jit-off, jit, jit-deopt, handoff and handoff:3, full jitcheck, the four floor units in jit and deopt, and all 197 jit_* kernel programs in jit and jit-deopt. The declines where the old gates would have entered were all stale (test_cprofile in `on`: 54), the kill the C gate made before.
- Question 7: GUARD_ENTRY is never taken. jitcheck sharedcode reads `poll 0` on master and on both stages, and the stage 1 fatal never fired anywhere above.
- B7: stage 1 put the find before the wait. My probe (two functions of one code, the first's unit made to leave by an int-to-float global) does not reproduce B7 on master: identical counts on master and stage 1. The stage 0 probe is not in the tree. Measured cost of the new order: `-c pass` (JIT on) +0.5% instructions. It makes 27,922 entries that wait walk the unit list first; a profile shows unit_for+find+lu_jit_enter at 2.65% of samples against 1.85% for lu_jit_enter on master. Reverted in a third commit: the wait is tested first again, for interpreter entries only. After that `-c pass` reads +0.11%. B7 stays open (speed only, bounded, not reproduced here).

Stage 2 (ebb64fe5): deleted the stub's dead, monitoring, tracing and hook tests (a serial unit's gate keeps its death test); OP_GUARD_ENTRY (IR kind, emitter, opt.c x2, ssa.c, eval_flow.c; the globals and builtins stay held as constants); the stage 1 C checks and counters; `lu_jit_run`'s quiescent report and disabled test.

Gate, each stage: the gate's kernel programs (entry_*, threads_*, tools_*, trace_*, jit_reclaim*, signals_*, jit_tools_toggle, jit_direct_calls, jit_rebound_frames, jit_calls, jit_class_callables, jit_gen_turns, jit_delegation, jit_namespace_collect, jit_shared_code_hot, jit_shared_code_globals (stage 2), jit_local_events_inlined, return_release) pass in plain, jit-off, jit and jit-deopt, and threads_* in handoff and handoff:3. The one exception is threads_kill_under_calls in jit-off at stage 1: `oracle varies`, the oracle's own output. Full jitcheck: 0 failing; compiled, ccalls, toolsoff, sharedcode, borrowed and entryfails PASS; the usual NOT_BUILT gaps. check-capi-ext PASS. Floor units test_monitoring, test_sys_settrace, test_sys_setprofile and test_cprofile in jit and deopt: 0 lost and 0 gained against master's binary (566 pass).

Numbers (instructions:u, pinned on a core lease, two runs per arm, master's binary against stage 2; the `-c pass` and pyflakes rows are after the wait-order commit):

| row | mode | master | stage 2 | change |
|---|---|---|---|---|
| -c pass | on | 128.87M | 129.01M | +0.11% |
| -c pass | always | 845.16M | 841.98M | -0.38% |
| pyflakes | on | 8.251G | 8.224G | -0.33% |
| pyflakes | always | 14.778G | 14.817G | +0.27% |
| Richards (harness 5 20) | on | 11.330G | 11.204G | -1.1% |
| Richards | always | 12.449G | 12.302G | -1.2% |
| DeltaBlue (harness 5 2000) | on | 5.553G | 5.432G | -2.2% (spread 2%) |
| DeltaBlue | always | 6.340G | 6.276G | -1.0% (spread 1.5%) |

Direct-call slope, tools/bench/ladder (instructions per call, the rec12-rec2 and meth12-meth2 steps):

| row | mode | master | stage 1 | stage 2 |
|---|---|---|---|---|
| op.call_positional | on | 181 | 182 | 166 |
| op.call_method | on | 222 | 222 | 209 |
| op.call_keyword | on | 182 | 182 | 167 |
| op.call_positional | always | 181 | 182 | 167 |
| op.call_method | always | 223 | 222 | 209 |

Stage 1 cost about +1 (not the predicted +5: the fatal branches are not taken). Stage 2 is -13 to -15 below master, inside the predicted -12 to -20. tools/jitcost row call: measured 413 on master and 377 after, against CALL_COST 420 (ratio 1.11), within the factor, so the constant is unchanged. Every jitcost row passes on both binaries.

Wait-order commit gate: the gate's kernel programs in plain, jit and jit-deopt; full jitcheck, 0 failing (sharedcode, compiled, ccalls, toolsoff and entryfails PASS).

State: stages 1 and 2 committed, plus the wait-order commit, on branch luc-entry1, not pushed; waiting for the adversarial reviews. Open: B7 (not reproduced; the find-first order costs `-c pass` +0.5%); B3 has no test (no hook can delegate); pyflakes with the JIT always on reads +0.27%, under the 0.3% bar, cause not looked for. Stages 3-5 not started.

## Review fixes (2026-11-25)

- M1: `lu_monitoring_hook_changed` also runs `instrument_all_executing_code_objects`, as every other tools path does; a failure there is reported as unraisable, since the setter returns nothing. New tests/capi `hook_toggle` and `hook_toggle_run` set and clear a hook inside one call from a hot loop and require the loop's unit polls to grow with the iterations. The test passes without the fix too, so it does not show the defect. The likely reason (not verified) is that the interpreter's back-edge poll brings the code up to the version. It stays as a guard on the hook path.
- L1: `lu_jit_run` finds the unit again after anything that may run a finalizer or collect: the fresh generator's dropped value as well as the cells prologue. A decline after the drop resumes at RESUME 0.
- L2: `enter()` counts an entry only when the gate passed, as the direct entry does.
- L3: `unit_for` refuses a watched code before the find, as `lu_jit_enter` did; `compile_at` no longer tests it. The predicate is still one function and one test site.
- Not tested (reviewer's list): a gate decline after the marker on a fresh generator; kernel gates with visible events pending; `lu_jit_direct` refusing while tracing. Also recorded: `lu_jit_finish` ends a generator that raises with `lu_gen_finish`, where the old `lu_jit_run` tail called `lu_frame_pop` on the generator's frame. That was probably an old bug, unreachable while `leaves_root` refuses generator roots. No test was added for it.

## Stage 3: the invariant and the windows (2026-11-25, before code)

Invariant. `lu_jit_collect` (a collection's and `lu_jit_reclaim`'s) runs with the world stopped and frees a dying unit only when no task's activation list names it (`hold`). Every other task is then stopped at a poll or detached. So a unit is safe to run while either (a) an activation naming it is linked from the running task, or (b) the running thread has not polled, detached, parked or run Python since it took the unit (from its code's list by `find`, from `compile_at` under the compiling flag, or the direct entry word from the code's state). Stage 3 moves the link into the unit, so every window (b) now spans C and machine code up to the prologue's link, and again from the epilogue's unlink to the instruction that leaves the unit's code. An allocation in a window would be harmless by itself (it only raises `LU_POLL_GC_SCHEDULED`, `runtime/interp/gc.c`; the collection runs at the next poll), but a finalizer it ran would not be; the windows below hold no allocation, decref or call into Python at all.

Unit side, common to every path. Frame entry `fn`: `push rbp; mov rbp, rsp`; five pushes; `sub rsp`; `mov r14, rdi`; `mov r15, rsi`; the result word's store; the gate (serial: load, store and set the unlocked word, load `dead`, branch; the carrier's load, the C stack compare, the poll word compare, branches); the link (`act.unit` store, `outer` load and store, `lea`, `jit_act` store). Direct entry (the stub): the same saves, `mov r14, rdi`, the gate, the globals and builtins compares, the depth room compare, the datastack loads and compares, then the same link, before the frame push, the INCREFs and `lu_jit_prologue` (which allocates cells and runs inside the activation, as today). A gate or stub decline leaves before the link: it restores the serial word and runs the pops and `ret` inside window (b).

C and emitted side, from the load to the call of `fn` or the entry:
- Interpreter entry (`ceval.c` `jit_enter:` -> `lu_jit_enter`): `unit_for`'s `find` (or `compile_at`, whose own `find`/`compile` run under `compiling`, ending with the `compiling` store), returning u; `enter()` calls `u->fn`. The debug print that ran `PyUnicode_AsUTF8` here moves after the return.
- C entry (`lu_eval` -> `lu_jit_run`), plain frame without cells, and generator resume after a yield: `unit_for`'s `find`; the C-entry marker's initialisation, `frame->previous`, `ts->current_frame`, `frame->instr`, `frame->last` stores; `enter()`. The debug print moves after the return.
- C entry with cells, and the fresh generator start: the sent value's DECREF and the cells (`prologue`, `PyCell_New`) run first, then `find` again (as stage 2 already does); after that find only the `instr`/`last` stores and `enter()`.
- `lu_jit_direct` (`call_function`, `function_vectorcall`): the code's state from `func_code`, the entry word (acquire), the pending-exception and tracing tests, the `takes` mask, the frame word's address, the call.
- Emitted direct call (`direct_call`): `func_code`, `co_executors`, the tag test, the `nargs` compare, the entry word's load and test, six register moves, `call r11`.
- On a decline (r is `LU_JIT_NONE`) `enter()` reads `u->version` and may `kill` u (`tools_moved`); `kill` takes the dying and bucket mutexes, which never detach, and allocates only under `-X lucifer_jit_events` (`PyUnicode_AsUTF8`, not a GC object). Nothing touches u after it.

Epilogue. Every path that ran past the link (RETURN, RAISED, YIELD, an exit's `lu_jit_deopt`, the stub's prologue error) jumps to the epilogue: `jit_act = act.outer`, a serial unit's unlocked word back, `u->stale` tested; then `lea rsp`, the pops, `ret` into the caller. A stale unit takes a cold copy: the same pops, then `mov rdi, u; mov esi, eax`, and a jump (not a call) to `lu_jit_left`, which runs `left(u)` and returns r to the unit's caller. The tail is a jump because `left` may free the unit: the last `left` of a stale unit runs `lu_jit_release`, which stops the world and reclaims it; a call from the epilogue would leave a return address into freed code. It comes after the unlink so that this stop does not see the activation and keep the unit. Between the unlink and the `ret` or `jmp` the unit's code runs with no activation, inside window (b): no instruction there polls or calls. The first point at which a collection can run is, for a fresh unit, the caller's next poll (a C wrapper, or the emitted site of a unit whose own activation is linked); for a stale one, `lu_jit_release`'s stop inside `lu_jit_left`, executing in C with only the caller's return address on the stack.

## Stage 3 (2026-11-25, branch luc-entry3, not landed)

Built (12fdcd04, test 493b7b65, invariant a7a2e283):
- The activation record (`struct lu_jit_act`) sits in the unit's machine frame. `link_activation` is emitted at both entries after everything that can decline: in the frame entry right after the gate, in the stub after the globals, depth and datastack checks and before the push, the INCREFs and `lu_jit_prologue`. The gate keeps the serial word in the record. The entry count (`entries`, and `entered` when `-X lucifer_jit_stats` was on at the build) follows at `entered`, shared by both entries: one place.
- Epilogue: unlink, a serial unit's word back, `u->stale` tested; a stale unit pops its frame and jumps to `lu_jit_left(u, r)`, which runs `left` and returns r to the unit's caller. Declines (gate or stub) go to one cold block that restores the serial word and returns without the unlink.
- Deleted: `struct lu_jit_call`, `CALL_SLOT`, `serial_exit`, `bump_entries`, the activation code in `enter()` (`lu_jit_enter`, `lu_jit_run`) and `lu_jit_direct`, the site's unlink, serial restore and stale call. The debug prints between the find and the entry moved after it. `serial_entry_dead` was already gone (stage 1 folded it into the gate).
- Departure: the site still needs the callee's frame on RETURN until stage 4 moves the frame end into the unit, so the entry's sixth argument is now `struct lu_frame **` (one word, `FRAME_SLOT`, written by the stub) in place of the record. The frame size is unchanged (40 bytes for result word, record and frame word).

Questions 5 and 6:
- 5: nothing between the find and the link polls, detaches, parks, runs Python or allocates; the windows are listed above. The cells and the fresh generator's DECREF already ran before `lu_jit_run`'s second find (stage 2 L1); the only allocating statements left in the windows were the two debug prints, now after the entry. Tests: `threads_kill_in_chains` and `jit_reclaim_threads` in handoff, handoff:3 and gc:1.
- 6 (by reading, no perf run: a stale tail runs once per stale unit's activations, too rare to sample): `perf.c` names a unit by its whole range in the map and gives the jitdump one frame-pointer CFI rule for it (CFA = rbp + 16 after `mov rbp, rsp`). The tail's pops and jump are inside the range, so a sample there is named after the unit, and its CFI is wrong in the same way as today's `ret` epilogue after `pop rbp`. Once the jump runs, the PC is in `lu_jit_left`, symbolized from the binary, and unwinding goes from it to the unit's caller with no unit frame, as for any tail call. Nothing new.

B9 (`-X lucifer_jit_stats`, `jit_compiled_calls.py 20000`): `unit fib@1 entries` 10 -> 964 under `always` (poll 958), 5 -> 489 under `on` (poll 484).

Code bytes per unit (same report, stats on, which adds `entered`'s 14-byte `lock inc` to every head unit): `on` 212,421 / 24 = 8,851 -> 213,826 / 24 = 8,909 (+0.7%); `always` 7,093,102 / 268 = 26,467 -> 7,047,232 / 268 = 26,296 (-0.6%). Each unit gains the link, count and unlink (about 35 bytes hot, 40 cold); each direct call site loses its unlink, serial and stale code (about 50 bytes, twice where self may be a bound method's).

Gate (head 12fdcd04): the 88 named programs (entry_*, tools_*, trace_*, threads_*, signals_*, jit_namespace_collect*, jit_reclaim*, jit_tools_toggle, jit_local_events_inlined, jit_direct_calls, jit_rebound_frames, jit_calls, jit_class_callables, jit_gen_turns, jit_delegation, jit_shared_code_collect, jit_release_frames) pass in plain, jit and jit-deopt (entry_tracebacks and threads_kill_under_calls XFAIL as on master; entry_tracebacks XPASS in jit-deopt) and in jit-off but threads_kill_under_calls (`oracle varies`, as at stage 1); the 54 threads_ programs in handoff and handoff:3; the 8 reclaim, collect and new programs in gc:1. jitcheck: 0 failing, the same ten GAP lines as master's binary. check-capi-ext PASS. Floor units test_monitoring, test_sys_settrace, test_sys_setprofile, test_cprofile: 566 pass, 26 fail, 1 error with both binaries in jit and in deopt, 0 lost. `threads_kill_in_chains` passes on master in every lane above and, in a fresh worktree built with plain `make -j4`, in plain, jit-off, jit, jit-deopt, handoff:3 and gc:1. It fires: under `on` 30,933 compiled calls, 13 units invalidated, 81 freed; under `always` 36,749, 145, 270.

Numbers, instructions:u, pinned on core leases, master 4f56066d vs head, two runs per arm alternating (identical to 0.1% between runs), DeltaBlue the minimum of three:

| row | mode | master | head | change |
|---|---|---|---|---|
| -c pass | on | 130.13M | 130.01M | -0.09% |
| -c pass | always | 851.66M | 850.99M | -0.08% |
| pyflakes | on | 7.806G | 7.775G | -0.40% |
| pyflakes | always | 14.160G | 14.122G | -0.27% |
| Richards | on | 11.211G | 11.266G | **+0.49%** |
| Richards | always | 12.324G | 12.370G | **+0.38%** |
| DeltaBlue | on | 5.4452G | 5.4510G | +0.11% |
| DeltaBlue | always | 6.3591G | 6.3524G | -0.11% |

Ladder slope (instructions per direct call, both runs equal): op.call_positional 167 -> 169 (on and always), op.call_method 209 -> 213, op.call_keyword 167 -> 169. Cycles per iteration went down in every rec and meth row under `on` (rec12 612/631 -> 587/594, meth12 674/668 -> 652/645). tools/jitcost row call: 389 -> 370 against CALL_COST 420 (ratio 1.08 -> 1.14), PASS; every row passes on both.

Why the slope and Richards rose: the measurements ran on cores with the JCC erratum, where `window_branch` pads every hot branch that would cross a 32-byte window with NOPs, which `instructions:u` counts. The change shifts every unit's layout. Builds of both arms with `jcc_erratum()` returning 0 (analysis only, not committed): slope rec 154 -> 154 (rec12 - rec2 1,540 -> 1,535: -0.5 a call), meth 193 -> 193 (-0.6); Richards -0.07% (on), -0.07% (always); DeltaBlue -0.32%, -0.25%. The logic's own saving is about one instruction a call, not the design's four: the design's count took the stub's frame-word store as gone (it goes with stage 4's frame end) and counted the stub's record stores as seven where there were six; the link costs one more (`lea`, the record no longer at hand in R9).

Acceptance: not met as written (Richards over 0.3% in both modes, slope up 2 and 4) with the padded code; met with padding out of the picture. Left to the coordinator.

State: stage 3 committed on luc-entry3 (a7a2e283, 493b7b65, 12fdcd04), not pushed; waiting for the adversarial review. Stage 4 not started.

## Stage 4: the lifetime argument (2026-11-25, before code)

What can free a unit at a return. (1) A stop (`lu_jit_collect`, a collection's or `left`'s release) frees a dying unit no linked activation names; it needs every task at a poll, detached or stopped, so it cannot run while this thread executes compiled code outside a call. (2) Code dealloc frees every unit of the code at once, with no stop: it runs when the code's last reference goes, and the frame's own `code` reference can be that one (a function whose last reference is the frame's, holding the last reference to a code object of its own). Freeing the function can do the same through its `func_code`. So nothing that may drop the frame's `func` or `code` reference to zero, or run a finalizer that could, may execute in the unit, and no address in the unit may be on the stack when it happens.

The unit's return (OP_RETURN of a root, RETURN or RAISED), in order:
- OP_UNLINK (stage 4: every root): `current_frame = previous`, and the depth back (`py_recursion_remaining` or `kernel_room`), before the releases as `lu_frame_drop` does (B6). A generator's state as before.
- The releases of the locals and the stack (finalizers run here, the activation still linked: a stop keeps the unit; the frame's `func` and `code` still hold their references, so no dealloc of the code can happen here).
- The value stored, the owner restored.
- Plain roots, fast path: no frame object; the frame not the first of its datastack chunk; `func` and `code` each immortal, or owned by this thread with a local count above one. All are tested before either count moves; then the two counts drop by one each (neither reaches zero, so nothing is freed and nothing runs) and `datastack_top = frame`. Then the epilogue: unlink the activation, the serial word, `u->stale` tested; `ret`. The last instruction executed in the unit is the `ret`, and the unit cannot be freed before it: no reference reached zero, and no stop can run before this thread polls in its caller.
- Anything else (a frame object, a chunk's first frame, a count of one, another thread's object, any generator root): the frame is not ended in the unit. The epilogue unlinks the activation and restores the serial word, loads the four arguments (task, frame, r, unit) into argument registers, pops the machine frame, and jumps to `lu_jit_left(u, r, ts, frame)`. The last instruction executed in the unit is that `jmp`. Before it nothing was released that the frame owns, so the code and function still hold. After it, `lu_jit_left` runs `left(u)` first, which may stop the world and free u, but no activation names it and no address in it is on the stack; then it ends the frame (the second half of `lu_frame_drop`: the frame object or the references, the code, the datastack; or `lu_gen_finish`), which may free the code and with it the unit, again from C; it returns r to the unit's caller. `left` comes first because the end may free the unit through the code's dealloc, after which `left` would read freed memory.
- A stale unit with a fast-path end takes the same jump with no frame to end.

Callers then hold no frame of the unit's to read: `ceval.c` reads the caller, `ret`, the generator and kernel flags before `lu_jit_enter` and goes to `returned` (after `return_frame`'s drop) or the RAISED tail of `unwound`; `lu_jit_run` reads `ret` and the generator flag before the entry; the emitted site and `lu_jit_direct` take the value. For DEOPT and ERROR the unit names the root frame in the result word (as the design's 2.1 says), so the site needs no frame word.

## Stage 4 and the review of stage 3 (2026-11-25, branch luc-entry3, not landed)

Ruling: stage 3 lands with stage 4, judged as a pair against master.

Built:
- OP_UNLINK gives the depth back for every root (B6's order: before the releases).
- A plain root's return ends its frame in the unit (`frame_end`, emit.c) when nothing can be freed. The conditions: no frame object, not its chunk's first frame, and the function's and code's counts each immortal or this thread's and above one. All are tested before either count moves; then the two decrements and the `datastack_top` store. Anything else, and every generator root, goes to the epilogue's `end` path: unlink, serial word, then `lu_jit_left(u, r, ts, frame)` reached by a jump after the pops. It runs `left` first, then `lu_frame_end` (`lu_frame_drop`'s second half, now split out) or `lu_gen_finish`.
- The eval loop reads the caller, `ret`, the kernel and generator flags before `lu_jit_enter`, and goes to `returned` (after `return_frame`'s drop) or `unwound_caller` (the RAISED tail). The generator value goes through `lu_gen_returned` (gen.h), shared with `return_frame` and `lu_jit_run`.
- `lu_jit_finish` no longer ends frames. `direct_done` keeps only the compare, `takes_back` and the value; its non-RETURN path is cold. DEOPT and ERROR name the unit's frame in the result word (exits and the stub's prologue error), so `FRAME_SLOT` and the entry's sixth argument are gone. `lu_jit_direct` and `lu_jit_run` no longer pop.
- Post-condition checks (depth and `datastack_top` as before the push) ran as fatal checks for the gate and were dropped in 422202f6 without measuring their cost. The current-frame check was dropped earlier (fca16108). It fired on test_sys_settrace, legitimately: a walker that makes an inlined caller's frames real during a call gives the callee a new `previous` (the caller's record, exit.c), which is current after the return.
- Review T1: `lu_code_free` fills freed code with int3 and `free_unit` overwrites the unit with a pattern before `free`, unconditionally (frees are rare; the pair's numbers below include it). Stats now print `stale leaves` and `frames ended in C`.

Review L1 (policy shift). `u->entries` now counts an interpreter or C entry when the run starts, where `bump_entries` counted after `fn` returned. Within the first activation of a C-entered unit (a loop unit entered once at a back-edge), entries read 1 where they read 0, which changes two things:
- `hot_exit`'s `loop = LOOP_CALLS * entries` needs 64 calls where it needed none.
- `unit_losses` computes a per-entry loss instead of returning 0.

The ledger lanes in jitcheck, master against the pair: churn, learnprice, grown, megaiter and entrygrowth are identical. classtests' first line moved 208,839 -> 208,868 and typed's 634,530 -> 634,565 (+0.01%); their ledger rows are identical (classtests 55,537 first / 7,628 rebuilt; typed 72,256 / 17,828).

Tests (first, all pass on master in plain, jit-off, jit, jit-deopt, gc:1):
- `entry_frame_end`: frame objects kept past the return with f_locals; finalizers at the return that walk frames, rebind a folded global, replace the callee's `__code__`, or raise; a code whose last reference is the returning frame's; generators returning into for, next, sum and list; frames first in a datastack chunk; recursion returning near the limit, with the RecursionError depth identical over 30 tries.
- `threads_serial_decline`: a serial unit's direct entry declines at the recursion limit, then threads start.

Not added: the stub's prologue error after the link (needs `PyCell_New` to fail), and a split of the stale-tail count by entry kind (the tail cannot tell; `stale leaves` reads 2 to 4 on threads_kill_in_chains in both modes).

Gate (fca16108; 422202f6 only removes the checks and was verified in the fresh worktree):
- 94 programs (stage 3's 88 plus the two new ones, return_release, jit_return_release_kill, jit_release_order, frame_teardown) pass in plain, jit-off, jit and jit-deopt (same XFAILs as master).
- 55 threads_ programs pass in handoff and handoff:3; 15 reclaim, release, collect and new programs pass in gc:1.
- jitcheck: 0 failing, the same GAP lines (two counts differ by one).
- check-capi-ext PASS.
- The four floor units, master's binary against the pair in jit and deopt: 566/26/1 both, 0 lost.
- A fresh worktree at 422202f6 built with plain `make -j4` passes the three new programs in plain, jit-off, jit, jit-deopt, handoff:3 and gc:1.

Numbers, pair (422202f6) against master (4f56066d). Instructions:u, pinned on core leases, two runs per arm (DeltaBlue the minimum of three). The "no padding" column builds both arms with `jcc_erratum()` returning 0 (analysis only, not committed):

| row | mode | master | pair | change | no padding |
|---|---|---|---|---|---|
| -c pass | on | 130.13M | 130.13M | +0.00% | -0.00% |
| -c pass | always | 851.72M | 846.60M | -0.60% | -0.53% |
| pyflakes | on | 7.813G | 7.751G | -0.79% | -0.91% |
| pyflakes | always | 14.201G | 14.062G | -0.98% | -1.10% |
| Richards | on | 11.211G | 11.192G | -0.17% | -0.73% |
| Richards | always | 12.324G | 12.300G | -0.19% | -0.73% |
| DeltaBlue | on | 5.4457G | 5.4089G | -0.68% | -1.09% |
| DeltaBlue | always | 6.3613G | 6.3138G | -0.75% | -0.98% |

Slope (instructions per direct call, tools/bench/ladder, both runs):

| row | mode | master | pair | no padding |
|---|---|---|---|---|
| op.call_positional | on | 166 | 166 | 154 -> 148/149 |
| op.call_positional | always | 167 | 166 | 154 -> 149/148 |
| op.call_method | on | 209 | 208 | 193 -> 188 |
| op.call_method | always | 209 | 208 | 193 -> 188 |

Code bytes per unit, stats off (sizes the code dump reports):
- jit_compiled_calls: on 8,424 -> 8,389 (-0.4%); always 23,964 -> 22,780 (-4.9%).
- pyflakes: on 38,996 -> 36,514 (-6.4%); always 47,317 -> 43,495 (-8.1%).

tools/jitcost row call: 372 against CALL_COST 420 (ratio 1.13), PASS, so the constant is unchanged.

Acceptance: every row is better or equal with padding on, in both modes. The slope fell about 5.5 a call without padding (the design's 3 + 5 = 8 was optimistic), and 0 to 1 with it.

State: stages 3 and 4 committed on luc-entry3, not pushed; waiting for the adversarial review of stage 4. Stage 5 not started.
