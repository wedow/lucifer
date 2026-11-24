---
id: luc-ee8q
status: open
deps: []
links: []
created: 2026-11-24T13:38:59Z
type: feature
priority: 2
assignee: Greg Wedow
parent: luc-orn7
---
# bare calls from compiled code into C leaf primitives

C underneath compiled code is 45.8% of steady-state cycles over 14 programs (luc-mopl), with Python-written kernel code calling C primitives at 21.4%; a published primitive call costs 100 to 120 instructions in line plus glue in C. This design lets a primitive that runs no Python, never parks and observes no frames be called as a typed bare call, with a published slow arm only on its rare branch. Its own conservative estimate is 3.5 to 4.5% of all cycles (the generic call and attribute rows, 15.7%, stay published and are other tickets' work), so it is not a first-order lever: build S0 (the publish invariant check, a safety net for every bare path that exists today) and S1 (the leaf audit) when a lane is free; S2 to S4 wait on the census of how many primitive call sites have proven argument kinds (open question 2).


## Design (2026-11-24)


## 0. Summary

Most of this mechanism already exists, used for a handful of operations. A helper whose effects are only "may raise" or nothing at all (`fx` ∈ {0, `FX_RAISE`}) is already called with typed arguments in registers, without publishing, holds or an argument vector:
- the fixed-arity path of `call_helper` (`runtime/jit/x86_64/emit.c:2119-2131`);
- examples: `H_str_hash`, `H_dict_keys_id`, `H_str_concat`, `H_list_push`, `H_dict_lacks`;
- `eval_prims.c:292-305` already lowers three primitives to unboxed word helpers this way.

Primitives miss this for three reasons:
1. The primitive side has a single bit, `LU_METH_NOPY`. Its helper `H_prim` is still `FX_SEEN`, so it publishes and takes holds (`helpers.c:1777`). Every other primitive goes through `H_prim_call`, which is `FX_PY` (`helpers.c:1778`).
2. Many primitives that never run Python are declared `FAST`, which means "may run Python". Examples: `dict_find`, `object_id`, `iter_new`, `str_encode`, `native_hash`, `range_iter`.
3. The rare dangerous branch of a leaf decides the whole call: a contended lock that parks, a release on a race path, a key that needs `__eq__`.

The proposal has three parts:
- **(i) A leaf contract,** checked at run time by an invariant that costs nothing in compiled code: Python, parking and frame walks happen only while the innermost compiled root has published.
- **(ii) Leaves declared as typed helpers,** plus one table in the evaluator that maps a primitive to its leaf and the argument kinds it needs. This generalizes `eval_prims.c:292-305` and `lu_ev_prim_words`.
- **(iii) Declining leaves.** The leaf answers "declined" before it has any effect. The evaluator builds the slow arm with the existing `lu_ev_split`, and that arm is today's published `H_prim_call`. No emitter changes are needed for (b).

Expected gain, conservatively: about 3.5-4.5% of all steady-state cycles over the 14 programs. Roughly 2% is C glue (h_prim_call, argument checks and unboxing, result boxing, locks) and 1.5-2% is compiled-side publish, holds and argument vectors.

## 1. How it works today

### Declaration and registration

- **Primitives** are `PyMethodDef` tables per file, built with macros:
  - `PURE(name)` = `METH_FASTCALL | LU_METH_NOPY` (e.g. `prim/str.c:1338`, `prim/type.c:2884`, `prim/dict.c:561`);
  - `FAST(name)` = `METH_FASTCALL` (`prim/storage.c:1312`);
  - `FRAME(name)` = `LU_METH_FRAME` (`interp/gen.c:757`).
- **Signature:** `PyObject *prim_x(PyObject *m, PyObject *const *args, Py_ssize_t nargs)`. It checks its own arguments (`ARGS`, `LIST`, `STR`, `IDX`, `SSIZE` macros, e.g. `prim/storage.c:17-25`) and boxes its result (e.g. `prim_str_find` returns `PyLong_FromLongLong`, `prim/str.c:189-193`).
- **Registration:** all tables are added to `_lucifer` (`prim/module.c:282-312`).
- **Flag meanings** (`lucifer.h:31-42`): `LU_METH_NOPY` = "never runs Python (may allocate and raise)"; `LU_METH_FRAME` = returns a frame to run.
- **Count** (by grep of the macros and the explicit `{ "name", (PyCFunction)...` entries): about 579 distinct primitive names, of which 80 are `LU_METH_NOPY` (79 `PURE` plus `str_join`, `prim/module.c:379`) and about 412 `FAST`.

### How the kernel names a primitive

- Kernel modules import primitives as module globals, e.g. `from _lucifer import (... list_append, ...)` (`lib/kernel/list.py:9-13`), and call them as ordinary functions.
- A thin wrapper such as `_append` (`list.py:108-110`) is registered for the call instruction with `kernel_set('list_append', _append)` (`list.py:335`).

### How eval lowers a primitive call (`eval_call.c`)

1. `primitive_name` (`eval_call.c:18-34`) accepts a constant `PyCFunction` whose `m_self` is the `_lucifer` module and which is not a kernel def.
2. `LU_METH_FRAME` primitives are called as C calls them (`eval_call.c:1132-1138`).
3. `seqiter_next` and `rangeiter_next` get their own lowerings (`eval_call.c:1150-1154`).
4. Otherwise (`eval_call.c:1155-1176`):
   - `lu_ev_prim` is a chain of per-name transfer functions (`eval_prims.c:234-476`): float ops, size, `seq_get`, word helpers, `str_ord`, `str_item`, `str_cmp`, `type_lookup` folded under a class version, `kind`, `alloc` made virtual, `field_get`/`field_set`, `typed_get`/`typed_set`, and `list_append` → `H_list_append` for an exact list (`eval_prims.c:471-474`).
   - Then `lu_ev_prim_words` (`eval_prims.c:482-510`), which passes index and hash operands as machine words through `H_prim_words`.
   - Otherwise `H_prim` if `LU_METH_NOPY`, else `H_prim_call` (`eval_call.c:1170`).
   - Result types come from a name list (`lu_ev_prim_type`, `eval_prims.c:212-232`).

### The C side of a primitive call (`helpers.c`)

- `h_prim` (`helpers.c:441-448`): an indirect call through `m_ml->ml_meth` with the argument vector.
- `h_prim_call` (`helpers.c:1227-1240`):
  - `lu_jit_stack_low()` (a TLS load of the carrier, then the floor, `jit.h:71-75`), which declines when the C stack is low;
  - `t = lu_current_task()` (TLS, `lucifer.h:1027-1031`);
  - `t->pin++` / `pin--`, the call, then `polled()`, which reads `carrier->poll` and may run `lu_poll_slow` (`helpers.c:1036-1044`).
- `h_prim_words` (`helpers.c:1292-1306`) rebuilds boxed ints on the C stack (`stack_int`, `helpers.c:1260-1290`) and calls `h_prim_call`.

### The effect flags (`ssa.h:362-368`) and what each licenses

| flag | what readers do with it |
|---|---|
| `FX_RAISE` | The helper gets a frame state (`eval_emit.c:333-335`), an `EXIT_ERROR` exit on NULL/-1 (`emit.c:5111-5114`, checks `5139-5148`), and a handler's catch (`lu_ev_caught`, `eval_emit.c:337-339`). |
| `FX_PY` and `FX_SEEN` | Same treatment in every reader; no code ever tests one without the other. Readers: `g->observed` → `GUARD_FRAMES` after the bytecode (`eval_emit.c:336`, `584-588`); `lu_publishes` (`target.h:486-506`) → `publish`/`unpublish` with the record address, `unlocked_pause`, inlined depth and holds (`emit.c:1813-1872`); `unsettles` keeps checks of the frames and the dead word (`facts.c:72-89`); not `borrow_safe` (`opt.c:452-466`); `bare` pause/again when there is no state (`emit.c:2092-2097`). By comment, `FX_SEEN` means "may release a value of any type, or park". |
| `FX_ALLOC` | Defined (`ssa.h:366`), read nowhere. Delete it. |
| `FX_DECLINES` | `GUARD_NOT_NULL imm 1`, which exits to the interpreter on `LU_JIT_DECLINED` = `(PyObject*)1` (`eval_emit.c:340-344`, `emit.c:4488-4499`, `target.h:610`). |

### IR that already calls C without publishing, and why that is safe

- **Helpers with `fx` 0 or `FX_RAISE`** (`helpers.c:1744-1849`): `box_int`, `box_float`, `attr_load`, `dict_lacks`, `str_concat`, `build_slice`, `build_string`, `str_hash`, `dict_keys_id`, `dict_stamp`, `str_char`, `range_iter`, the `*iter_new` helpers, `build_list`, `build_tuple`, `fields_new`, `new_object`, `new_dict`, `list_push`, `seq_concat`, `float_pow`, `bind_method`, `bytes_concat`, and the exception plumbing.
- **Other IR:** `OP_DECREF imm 0` (types whose freeing runs nothing: `quiet`, `eval_emit.c:497-504`), `OP_TRUTH imm 1`, and `INS_FRESH` stores (`target.h:489`).

They are safe because none of them can run Python, release an object whose type runs code, park, or observe frames or counts. `list_push` writes to a list no other thread has seen (`helpers.c:185-191`). Allocation never collects: it only sets `LU_POLL_GC_SCHEDULED` (`interp/gc.c:90-98`), and "Collection runs only here" in the poll (`interp/poll.c:92-96`).

Parking does need publishing. A stop-the-world walker may read the frames, and the "lock elision on private objects" argument relies on that (`jit-architecture.md` §10). This is why `h_list_append` is `FX_SEEN` (its critical section may park, `helpers.c:167-175`; `lock.c:33-110` → `_PyParkingLot_Park` detaches, `parking_lot.c:97`). It is also why parked slow paths publish with all registers saved (`emit.c:2165-2290`), without holds (`a->parking`, `emit.c:1820`).

### How exceptions come back

- The helper returns NULL (or -1 for `REP_BIT`/`REP_I64`) with the exception set.
- Compiled code jumps to the `EXIT_ERROR` exit of the instruction's state, or to the unit's handler when `INS_CAUGHT`.
- `h_prim_call` additionally runs pending Python-visible poll reasons after the call (`polled`).

### Cost today

- **In line:** `HELPER:prim` 119, `prim_call` 103, `call` 392 instructions. The holds are 60-90% of a published helper's in-line instructions (`luc-ne54`; the per-helper table there gives `prim_words` 80+115 instructions in holds and releases).
- **In C:** `h_prim_call`, the TLS lookups, `ml_meth` dispatch, argument checking and unboxing (`index_arg`, `stack_int`, `lu_long_as_long` appear as leaves in rows 11, 18, 39, 44), and boxing of int results (allocation in rows 21, 36, 43).

## 2. Catalogue: what the C in the 56 rows can do

Method: I read the C named in each row and followed its calls one level down (two for locks, dict and type lookups).

Hazards found:
- **Runs Python:** `__eq__`/`__hash__`, watchers, finalizers through a release.
- **Parks:** a contended `PyMutex` → parking lot → detach (`parking_lot.c:97`).
- **Releases an object of unknown type.**
- **Writes something a dependency names:** a watched dict (`changed`, `object/dict.c:150-164`), or `lu_jit_release`, which stops the world (`object/dict.c:876, 909, 931, 953`).
- **Observes frames or counts.**

Allocation never collects (above). Raising here means builtin exceptions only.

### Class (a): bare as is once the argument types are proven (13 rows, 4.87% of all cycles)

| row | primitive | what the C does |
|---|---|---|
| 11 | `str_slice` 1.02 | Allocates only (`prim/str.c:293-301`). |
| 13 | `str_join` 0.95 | Tuple case: `join_items`, which allocates and raises TypeError (`prim/str.c:693-720`). List case takes a critical section: lock-free while serial, a try-lock in parallel (class b). |
| 20 | `str_find` 0.57 | `find_in`; nothing raises once the types are known (`prim/str.c:150-193`). |
| 29 | `str_encode` 0.33 | Built-in codecs only (`codec_of`); errors return a tuple to the kernel (`prim/str.c:1091-1130`). |
| 36 | `object_id` 0.28 | Boxes an address (`prim/builtins.c:16-20`). |
| 38 | `bind` 0.27 | `descr_bound` allocates a bound object (`object/descr.c:1640-1648`). Inference: no Python. |
| 43 | `native_hash` 0.22 | Native hashes only, MISSING otherwise (`prim/dict.c:188-195`). |
| 44 | `box_get` 0.22 | Reads a field. |
| 47 | `iter_new` 0.21 | Allocates (`prim/storage.c:909-922`). |
| 50 | `int_format` 0.21 | Allocates; raises ValueError past the digit limit. |
| 53 | `alloc` 0.20 | Leaf only when `tp_alloc == PyType_GenericAlloc` (`prim/type.c:2385-2389`). |
| 54 | `str_subtype` 0.20 | Same `tp_alloc` condition (`prim/str.c:1307-1320`). |
| 55 | `range_iter` 0.19 | Allocates (`prim/storage.c:1246-1254`). |

### Class (b): bare fast path, published slow path on a rare branch (14 rows, 8.70%)

| row | site | rare branch that must decline |
|---|---|---|
| 8 | `type_lookup` 1.14 | Method-cache hit and lock-free MRO walk are leaf work (`prim/type.c:481-595`). Rare: the dict-lock path (`found == -2`, may park) and a `Py_DECREF` on a version race (`type.c:567-570`, `walk` 515-524). |
| 6, 14 | `list_append` 1.18 + 0.80 | `lu_list_append` (`prim/storage.c:466-478`): growth retires the old array to QSBR, so it is leaf work. Rare: a contended critical section. |
| 18 | `dict_probe` 0.72 | Leaf for native keys. A candidate needing `__eq__` returns a tuple for the kernel to compare (`prim/dict.c:105-113`). |
| 19 | `dict_find` 0.63 | Already returns NotImplemented instead of running `__eq__` (`prim/dict.c:116-124`). Rare: the lock path, and a stale `Py_XDECREF` (`object/dict.c:673-698`). |
| 21 | `tuple_hash` 0.53 | `lu_tuple_hash` → `PyObject_Hash` of the items; decline when an item is not natively hashed (`prim/storage.c:370-379`). |
| 31, 23 | `seqiter_next` 0.30 + 0.49 | Exhaustion releases the sequence (`Py_XDECREF(dead)`); there is a lock path (`prim/storage.c:955-990`). |
| 37 | `instance_load` 0.27 | Inline values are leaf work; the dict path takes a lock (`prim/type.c:2497-2507`). |
| 39 | `dict_insert` 0.26 | A watched dict invalidates units and stops the world; a contended lock (`prim/dict.c:211-224`). |
| 52 | `list_pop` 0.20 | Lock contention only; the item's reference goes to the caller (`prim/storage.c:814-831`). |
| 7 | `setitem` 1.16 | Exact dict, native key, unwatched, and the old value of a quiet type or absent (`lu_dict_put_native` / `lu_dict_replace`, `object/dict.c:863-879`). Anything else declines. |
| 16, 45 | `attr_store` 0.80, `slot_store` 0.22 | They release the old value; leaf when that value is of a quiet type. |

### Class (c): stays a full published call (19 rows, 15.72%)

- **Generic calls** (rows 1, 5, 12, 26, 46): call 3.71, CALL_FUNCTION_EX call 1.84, `call_spread` 0.98, `_scan_once` 0.37, `call_ex` 0.21.
- **Generic attribute access:** `load_attr` 2.83, `load_method` 1.03, `store_attr` 0.80, `super_attr` 0.77.
- **Other generic helpers:** `binary_op` 0.52, `contains` 0.46, `make_function` 0.39 (function watchers), `for_iter` 0.31 + 0.30, `store_global` 0.28 (`lu_jit_invalidate`), `raise` 0.25, `binary_slice` 0.25, `list_extend` 0.21.
- **Observes frames:** `frame_at` 0.21.

### Other rows

- **Releases at DECREF, not calls** (7 rows, 5.92%): rows 3, 4, 27, 28, 32, 34, 48.
- **Already bare** (3 rows, 1.53%): `str_concat` 1.09, `dict_lacks` 0.25, `list_push` 0.19.

**Totals:** 13 + 14 + 19 + 7 + 3 = 56 rows, 36.7% of all cycles, which matches 80% of the 45.8% C share. The mechanism addresses classes (a) and (b): 27 rows, 13.6% of all cycles. Much of that is the operations' own work, which stays.

## 3. The mechanism

### 3.1 The leaf contract

A leaf:
- runs no Python;
- releases only objects of quiet types, or none;
- never parks or detaches (a lock is try-only);
- never reads frames, reference counts or `ts->current_frame`;
- never writes a watched dict or a type;
- never polls.

It may:
- allocate;
- raise a builtin exception (NULL/-1 with it set);
- write to objects it is handed, under the object's lock or while serial;
- release an object of unknown type on a race path only, through `_PyObject_XDecRefDelayed` (`thread/qsbr.c:283-289`), which runs at the next poll.

A declining leaf returns a declared sentinel before any effect: no write, no reference taken or dropped, no lock still held. The sentinel is `LU_JIT_DECLINED` for object results and `INT64_MIN` for word results.

Of the existing flags, a leaf is simply `fx ∈ {0, FX_RAISE}`. No new flags are needed. `FX_ALLOC` is deleted, and the `FX_SEEN` comment gains "or takes a lock that may park".

### 3.2 Where leaves are declared

**C.** Each leaf is a function `lu_leaf_<name>` next to its primitive (in `runtime/prim/*.c`, declared in `prim/prim.h`), with a typed signature. Example:

```c
int64_t lu_leaf_str_find(PyObject *s, PyObject *sub, int64_t start, int64_t end, int64_t last);
```

- The generic `prim_<name>` becomes the wrapper: argument checks, unbox, call the leaf, box. There is one body.
- A leaf that needs the task takes it as its first argument (kind `TASK`, from R14), so it does no TLS lookup.
- Each leaf gets an `H_` id: one line in `LU_HELPERS` (`target.h:382`) and one in `lu_helpers[]` (`helpers.c:1744`), with its result rep and fx. This is the pattern `H_str_hash`, `H_dict_keys_id` and `H_dict_stamp` already follow.

**Eval.** One declarative table in `eval_prims.c` replaces `words[]` (`eval_prims.c:294-298`), the `prims[]` of `lu_ev_prim_words` (485-490) and the `list_append` branch (471-474). Each row gives:

```text
{ prim name, helper id, argument kinds[≤6] (OBJ, STR, LIST, DICT, TYPE, TUPLE, I64, F64, BIT, TASK),
  result (OBJ new reference | I64 | F64 | BIT) and its Python type, declined sentinel or none }
```

`lu_ev_leaf` is the one generic lowering:
1. Prove each kind: `lu_ev_type`, or `lu_ev_learn_type` / `lu_ev_learn_kind` with their guards, as `str_ord` does today. If a kind cannot be proven, fall back to today's path.
2. Unbox `I64`/`F64` arguments (`lu_ev_unbox_int` / `lu_ev_unbox_float`).
3. Emit `HELPER(H_leaf)`.
4. Box the result virtually (`lu_ev_box_int`), so nothing is allocated unless the value escapes.

**Ownership.** Arguments are always borrowed. This is the existing helper rule: "None keeps a reference it is given" (`ssa.h:369-370`). A leaf that stores an argument takes its own reference. An object result is a new reference (`lu_ev_own`); immortal sentinels such as MISSING, NotImplemented and bools are new references at no cost. Leaves do not consume arguments.

### 3.3 Lowerings

**(a) Bare call.** Exactly today's fixed-arity `call_helper`:
- `parallel_moves` into RDI…R9 and XMM;
- `mov r11, fn; call r11`;
- `test rax, rax; jz err` only if `FX_RAISE`.

About 4-10 instructions, with:
- no publish, holds or vector;
- no `unlocked_pause` (`emit.c:2092` adds it only for PY|SEEN);
- no `GUARD_FRAMES` (`observed` stays 0);
- no frame state unless it can raise.

The call is `borrow_safe` (`opt.c:460-462`), so borrowed list items can stay uncounted across it. Because it does not publish, the register allocator does not force the state's values to memory.

**(b) Declining leaf.** In the evaluator, no emitter change:

```text
v   = HELPER(H_leaf, typed args)              ; bare
bit = CMP v, sentinel   (OP_CMP_I, or OP_IS against LU_JIT_DECLINED)
lu_ev_split(c, bit, &s)
    slow arm: today's lowering (H_prim_call / H_prim_words with the boxed arguments:
              published, holds, GUARD_FRAMES by settle); for a word result, unbox the slow
              result (it has the primitive's declared type)
lu_ev_split_else(c, &s)
    fast arm: v
lu_ev_split_end(c, &s)                       ; the join: phi, or a virtual box of a raw phi
```

`lu_ev_split` exists and is used for exactly this shape (`eval.h:427-445`; uses at `eval_call.c:859, 1010`, `eval_iter.c:144`). The slow arm publishes its own state. Its settle (the `GUARD_FRAMES` on its path) covers walkers and dependencies taken back while Python ran. The fast arm needs nothing. A (b) leaf is not `borrow_safe`, because its slow arm may release.

**(c)** Unchanged.

**(d) In line.** A few leaves become IR when small: list append when capacity allows, on a list that is private or in a serial unit (the `facts.c` `locks_in_line` / `INS_UNLOCKED` rule), with the bare leaf as the slow path. Already in line: `str_len`, `size`, `str_ord`, `str_cmp1`.

### 3.4 Locks and the serial state

One new C helper, `lu_leaf_lock(t, op)` / `lu_leaf_unlock`, about 25 lines in `thread/critical_section.c` next to `PyCriticalSection_BeginMutex` (`critical_section.c:216-231`):
- **Serial:** count `unlocked_c` (`lu_unlocked_begin`, `lucifer.h:1233-1250`), take no lock.
- **Parallel:** one `lock_fast` CAS on `ob_mutex` and push the section. On failure, return 0 and the leaf declines. It never calls `_PyCriticalSection_BeginSlow`.

The task's `unlocked_jit` stays 1 across a bare leaf in a serial unit. That is sound because a leaf makes no task and runs no Python, and the switch to parallel waits for `unlocked_jit == 0` or a detach (`task.c:89-104`). A leaf is bounded, so it only delays the switch.

### 3.5 Collections and stop-the-world

A leaf never polls or detaches, so another task's stop waits for this task's next poll. That is the same as for any stretch of compiled code between polls, and the same as CPython's C builtins. Leaves do work bounded by their input, as those builtins do.

Collection never runs inside a leaf (`poll.c:92-96`; allocation only schedules it, `gc.c:97`). QSBR: a leaf reports no quiescent state, so its lock-free reads of key tables and MROs stay valid. Delayed releases are processed at the next poll (`poll.c:30-38`).

### 3.6 Settle after a bare call: none

What can kill a unit:
- another thread's `lu_jit_invalidate`, which is honoured at polls (`deps.c:1-15`);
- the switch to parallel (`LU_DEP_SERIAL` plus `LU_POLL_DEOPT_SELF`, `task.c:116-125`), honoured at polls;
- this thread writing something a dependency names: a leaf never does, because it declines on `_ma_watcher_tag & 1` (`object/dict.c:153`);
- freeing: units are freed only after `lu_jit_reclaim` with the world stopped. Units held through `task->jit_act` are skipped (`jit.c:768-807`), so a unit is never freed under its activation.

So nothing that reaches a bare leaf needs an owner or dead check afterwards. `facts.c` `frames_checks` already drops later `GUARD_FRAMES imm 0` when no published call precedes it (`facts.c:91-135`).

### 3.7 The check

Lucifer has no separate debug build. `Makefile:2` sets `CFLAGS = -O2 -g` and nothing defines `NDEBUG` for the runtime, so `assert()` is live (inference). So the check is always on and costs nothing in compiled code. It relies on an invariant that compiled code already writes: **Python runs, a task detaches, or frames are walked only while the innermost compiled root has published.** "Published" means `jit_desc != 0`; `unpublish` stores 0 (`emit.c:1840`).

Check function, a static inline in `lucifer.h`:

```c
lu_jit_assert_published(struct lu_frame *f)   /* f: the frame below what is about to run */
{
    if (f != NULL && f->owner == LU_FRAME_JIT && f->jit_desc == NULL)
        Py_FatalError("compiled code's unpublished call ran Python, parked or walked the frames");
}
```

Call sites:
- `lu_eval` (`interp/ceval.c:4329`) on `frame->previous`;
- `lu_jit_run`, `lu_jit_direct` and `lu_jit_enter` (`jit.c`);
- `lu_task_detach` (`thread/task.c:307`) on `current_frame`;
- `lu_frame_walk` / `lu_frame_current` (`interp/frame.c:267-309`).

That is one load and compare per Python entry from C. It catches a misdeclared leaf and any existing `fx`-0 or `FX_RAISE` helper that breaks the rule. Python run by finalizers enters through `lu_eval`, so it is covered.

This holds for existing bare paths: a helper with no state runs after `OP_UNLINK`, where `current_frame` is the caller, which is either published at its call or not JIT. `lt-98e0`'s native leaf callees link no record, and their caller is published. The stress lane `deopt:N` additionally takes the slow arm of (b) every Nth time, by counting the split's bit like a guard; that is not a new switch. Everything here was read, not run.

## 4. The top 15 rows under the mechanism

Assumptions:
- Compiled-side savings come from `luc-ne54`'s per-call sequences: prim 119, prim_call 103, setitem about 100, list_append about 70 instructions, against about 8 for a bare call and about 12 for an in-line append.
- C glue is estimated from the rows' leaf mixes. Work that stays is not counted.
- Types are assumed proven at 70% of sites. This is not measured (open question 2).

| # | row (% of all cycles) | becomes | compiled instructions removed | C glue removed | share removed |
|---|---|---|---|---|---|
| 1 | CALL `HELPER:call` 3.71 | (c); a constant `method_descriptor`'s kernel target bound at compile time (`luc-3s7n`, `lt-98e0`) | – | – | 0 counted |
| 2 | `load_attr` 2.83 | (c) | – | – | 0 |
| 3 | FOR_ITER release 2.50 | release work (`luc-eud7`/`uqyj`) | – | – | 0 |
| 4 | RETURN release 1.95 | same | – | – | 0 |
| 5 | CALL_FUNCTION_EX call 1.84 | (c) | – | – | 0 |
| 6 | `prim_call:list_append` 1.18 | (d) in line (serial or private); (b) leaf with try-lock otherwise | about 90 per call | `h_prim_call`, `pin`, `polled`, `ARGS`/`LIST`, critical section (lock 9%) | 0.70 |
| 7 | `setitem` 1.16 | (b) exact dict, native key, unwatched, quiet old value | about 90 | dispatch, `lu_tstate`, lock 6% (probe and insert stay) | 0.25 |
| 8 | `prim:type_lookup` 1.14 | (a) with a rare decline; best folded under the version (`luc-szry`) | about 110 | `prim_type_lookup` checks, `h_prim` | 0.17 |
| 9 | `str_concat` 1.09 | already bare | 0 | 0 | 0 |
| 10 | `load_method` 1.03 | (c) | – | – | 0 |
| 11 | `prim:str_slice` 1.02 | (a) `(STR, I64, I64) → OBJ` | about 110 | `IDX` unboxing (`index_arg`), `ARGS` | 0.10 |
| 12 | `call_spread` 0.98 | (c) | – | – | 0 |
| 13 | `prim:str_join` 0.95 | (a) tuple, (b) list | about 110 | the lock only | 0.05 |
| 14 | `HELPER:list_append` 0.80 | (d), as row 6 | about 60 | lock 10%, `lu_tstate`, the call | 0.40 |
| 15 | `store_attr` 0.80 | (c) | – | – | 0 |

**C side, top 15:** about 1.7% of all cycles.

**Compiled side** for rows 6-8, 11, 13 and 14 and the rest of the primitive family: `luc-ne54` step 2 gives holds 1.7, hold releases 2.5, arguments 0.8 and publish 1.0 over all published calls. If 40% of those belong to calls this mechanism converts, at a 70% conversion rate, about 1.7% is removed.

**Rest of classes (a) and (b) outside the top 15** (6.5% of all cycles): glue 15-20%, giving another 1.0-1.3%. Integer results such as `str_find`, `tuple_hash`, `object_id` and `native_hash` stop allocating boxes.

**Total, conservative:** about 3.5-4.5% of all cycles. On top of that, bare leaves are `borrow_safe` and keep values in registers, which affects value INCREF/DECREF (6.0) and spills (1.8); this is not counted.

## 5. What the kernel must change

1. **No protocol change.** The kernel already gives primitives a decline shape:
   - `dict_find`, `dict_put` → NotImplemented (`dict.py:131-136, 368-372, 439-449`);
   - `dict_probe` → candidate tuple (`dict.py:62-89`);
   - `native_hash` → MISSING;
   - `str_encode` → error tuple.

   Leaves return the same objects. For example, `dict_find`'s leaf returns the value, MISSING, or NotImplemented itself. The decline sentinel exists only for leaf-internal reasons: a contended lock, a race, a watched dict.
2. **Typed arguments come from the customization the kernel already relies on.** Inlined kernel code gets the user site's types (`docs/design/kernel-customization.md` §1), and its own `kind()` checks guard the rest. For example, `_find` (`str.py:99-105`) reaches `str_find(self, sub, start, end, last)` with `self` a str, `sub` checked, and `start`/`end` constants when omitted.
3. **Where the wrapper is the real cost:**
   - Star-argument methods such as `str.find(self, *args)` (`str.py:499-500`) and `startswith` cost a CALL_FUNCTION_EX or `call_spread` when not inlined (rows 5 and 12, 2.8%). That is call binding (`luc-3s7n`), not this mechanism.
   - Generic kernel units that are not inlined (megamorphic receivers) call primitives with unknown types. Their leaves apply only where the per-context profile proves the kinds.
4. **Thin wrappers bound straight to their primitive?** Not as a separate mechanism. Inlining a thin wrapper (`_append`, `list.py:108-110`; `__len__` → `size`; `_get`) already binds the call to the primitive, and with (a) or (d) that costs a few instructions. Where they are not inlined, the cause is either a constant `method_descriptor` called through `h_call` (row 1), which should be bound and inlined at compile time like any known function (`lt-98e0` and `luc-3s7n` territory), or the inline budget. Inference: counting a callee whose body is "call one primitive with the parameters" as zero against the budget would be enough, and is a one-line change in the budget accounting.

## 6. Stages, smallest safe first

Each stage is test first and passes `tools/floor quick` (kernel programs in plain, `jit` and `jit-deopt`).

### S0: the publish invariant (about 40 lines)
- **Files:** `lucifer.h` (the inline check), `interp/ceval.c` (`lu_eval`), `jit/jit.c` (three entries), `thread/task.c` (`lu_task_detach`), `interp/frame.c` (walkers).
- **Tests:** the hazard programs below pass on master and keep passing with the check. Run the daily floor once to flush out existing violators before anything relies on the check.
- **Share removed:** 0.

### S1: leaf audit of the generic paths (about 60 lines plus a written audit)
1. Audit the 80 `LU_METH_NOPY` primitives against §3.1. Those that release objects of unknown type or lock move to `FAST`.
2. `H_prim` becomes `FX_RAISE` only. It is still a vector call, but without publish or holds.
3. Re-declare the audited `FAST` leaves as `PURE`: `object_id`, `iter_new`, `range_iter`, `box_get`, `bind`, `str_encode`, `native_hash`, `str_subtype`. `alloc` is `PURE` only for `GenericAlloc` types, so it needs an evaluator check on the constant type instead.
4. Delete `FX_ALLOC`.

- **Files:** `prim/{str,type,dict,set,int,float,storage,builtins}.c`, `object/descr.c`, `jit/helpers.c`, `jit/ssa.h`.
- **Expected share removed:** about 1.0-1.5% (compiled side of `H_prim` sites and those `H_prim_call` sites).

### S2: typed leaves (about 250 lines)
- **C leaves:** `str_find`, `str_slice`, `str_join` (tuple), `str_count`, `int_format`, `object_id`, `native_hash`, `str_encode`, `iter_new`, `range_iter`, `type_lookup` (hit path), and the existing word helpers folded in.
- **Evaluator:** the `lu_ev_leaf` table and lowering, absorbing `lu_ev_prim_words` and `words[]`.
- **Files:** `prim/*.c`, `prim/prim.h`, `jit/target.h`, `jit/helpers.c`, `jit/eval_prims.c`, `jit/eval_call.c`.
- **Expected share removed:** about 1.0%.

### S3: declining leaves (about 300 lines)
- `lu_leaf_lock`; leaves for `list_append`, `list_pop`, `dict_find`, `dict_probe`, `dict_insert`/`dict_put`/`setitem` (exact dict), `seqiter_next`, `instance_load`, `tuple_hash`, `type_lookup` (miss and lock paths).
- The `lu_ev_split` slow arm, and `deopt:N` taking that arm.
- **Files:** `thread/critical_section.c`, `object/dict.c`, `prim/{storage,dict,type}.c`, `jit/eval_prims.c`, `jit/eval_call.c`, `jit/eval_ops.c` (setitem), `jit/x86_64/emit.c` (stress count only).
- **Expected share removed:** about 1.0%.

### S4: list append in line (about 100 lines)
- Serial or private lists; the leaf as the slow path; the `luc-6b0l` lock rule.
- **Files:** `jit/eval_prims.c`, `jit/facts.c`.
- **Expected share removed:** about 0.5-0.8%.

### Tests: one kernel program per hazard, compared with the CPython oracle

| program | what it exercises |
|---|---|
| `leaf_eq_keys.py` | Hot `d[k]`, `d.get(k)`, `k in d` and `d[k] = v` with keys whose `__eq__` and `__hash__` print, count, and mutate the dict mid-probe. |
| `leaf_release_finalizer.py` | A `__del__` that prints `sys._getframe(1).f_lineno` and the locals; reached through dict replace, `seqiter_next` exhausting a list that is the last holder, and list pop/clear; plus a `__del__` that rebinds a global the unit relies on. |
| `leaf_shared_list.py` | Two threads appending to and popping from one list, one growing it while the other iterates; output is a deterministic length and checksum. Runs in the handoff/threads lanes. |
| `leaf_raise.py` | Leaves raising inside `try` in hot loops: `int_format` past `sys.set_int_max_str_digits`, `str.encode('ascii')` strict, `''.join` with a non-str item, `type_lookup` on a non-type. Tracebacks and line numbers must match. |
| `leaf_collect.py` | Allocation-heavy leaf loops under `-X lucifer_stress=gc:N`, with `gc.callbacks` reading frames, and a second thread calling `gc.collect()` and `sys._current_frames()` while the main thread loops in leaves. |

### Count lanes (`tools/jitcheck` `GROWTH`)
- Add a census kind `CNT_leaf` (in `ssa.c:55-57`) for a HELPER whose fx lacks PY|SEEN.
- New lanes: `leaves` (`jit_leaf_calls.py`: `helper` 0 per iteration; `AT_LEAST` `leaf` ≥ k per iteration) and `leafholds` (`hold` 0 per iteration).
- A slow-arm lane under contention that allows at most 1/8 published calls per iteration.

## 7. Open questions

1. **Is the invariant clean today?** Whether any existing bare helper (`fx` 0 or `FX_RAISE`) or entry path runs Python, parks or walks frames with `jit_desc == 0`. Only the floor with S0 answers this.
2. **What fraction of primitive call sites have their argument kinds proven?** A census over the 14 rows' IR dumps of `H_prim`/`H_prim_call` sites whose arguments are typed would replace my assumed 70%.
3. **Does the join after `lu_ev_split` keep the fast result raw?** A virtual box on both arms merging to a raw phi, rather than materializing a box. Block placement of the slow arm: is there a cold-block order, or does the fast path pay a jump?
4. **Are delayed releases acceptable?** Releases on race paths (`type_lookup`'s version race, `dict_find`'s stale value) moved to `_PyObject_XDecRefDelayed` run finalizers at the next poll instead of at once. Inference: unobservable in a race. This is the owner's call.
5. **Do allocator hooks break the contract?** Under `tracemalloc` or `PyMem_SetAllocator` (`object/alloc.c` observed path), allocation goes through hooks that may lock (and so park) or run code. This applies equally to today's bare helpers (`box_int`, `new_object`). Should such hooks make leaves decline, or should every bare call be refused while allocation is observed?
6. **Is `lu_tstate`'s cost a call or TLS?** `lu_tstate` shows up as a leaf (rows 2, 14, 15) even though `lu_carrier` is `_Thread_local` (`thread/carrier.c:11`). The cost may be an out-of-line copy of the call rather than the TLS access.
7. **How is the published-call overhead split?** The share of HELPER overhead that belongs to the primitive family versus calls is not measured. The executed census per helper id under `-X lucifer_jit_stats` on the 14 rows would fix the compiled-side estimate in §4.
8. **External dict watchers.** Do watchers registered through `PyDict_AddWatcher` set bits other than `_ma_watcher_tag & 1`, and does `lu_dict_notify` test them? The leaf's "unwatched" test must cover both.

### Files
runtime/jit/eval_prims.c, runtime/jit/helpers.c, runtime/jit/eval_call.c, runtime/jit/target.h, runtime/thread/critical_section.c.
