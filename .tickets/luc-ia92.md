---
id: luc-ia92
status: open
deps: []
links: []
created: 2026-11-25T00:08:15Z
type: epic
priority: 1
assignee: Greg Wedow
parent: luc-orn7
---
# one mechanism per job: survey of duplicated and conflicting machinery

Survey of 2026-11-25 (read-only, tree at 118edfe8) of places where one job is done by several mechanisms across the interpreter, the tier, the object model and the kernel, asked for by the owner after the sister VM's machete pass. Finding: no 20 percent deletion is available inside the tier (its machete was the 2026-11-14 middle-end replacement); what exists is about 600 lines of that replacement's leftovers (some still costing a call on every warm builtin call and generic operator), capabilities that replacement dropped and never restored (yield from, await and async opcodes; classmethod and staticmethod folds through a class), and duplication ACROSS tiers that must agree by hand: one attribute-protocol predicate written about twenty times, four gates and four result handlers for entering compiled code, four call dispatchers, up to four type-feedback stores per site under three keys, seventeen kernel twins, nine frame-release sites. Children of this ticket take the items in the order of the notes below. The full survey follows.


## Survey (2026-11-25)

I found about 600 lines of verified dead code, three duplicated mechanisms whose disagreements show up in today's tickets, and no sign of a 20% machete inside runtime/jit. The machete-sized deletion already happened on 11-14: commit f9c82847 deleted ssa_build.c (6,913 lines) and ssa_opt.c (2,685). What remains is that rewrite's leftovers plus duplication that runs across the tiers rather than inside the JIT.

The survey was read-only: grep, git log, wc and awk on the tree at 118edfe8. "No callers" means I grepped `runtime/ lib/ tools/ tests/` for the name. Ticket figures are cited as the tickets give them. A "pp" figure from luc-mopl is percentage points summed over its 14 programs, so dividing by 14 gives the mean share per program. Where nothing was measured I say so.

## Ranked by lines deletable × confidence

### 1. Leftovers of the 11-14 middle-end replacement (f9c82847). About 600 lines, high confidence.

`git log -S` dates every item below to f9c82847. Each one lost its last emitter or caller there and was never deleted.

**a) Operator class tables, all dead.**
- `lu_jit_want_classes` (jit.c:2152) has no callers, so no `lu_opclasses` table is ever created.
- Everything that writes one therefore does nothing: `lu_jit_opclass_note` (jit.c:2169), `lu_jit_class_vote` (jit.c:2210), `lu_jit_observe_classes` and `lu_jit_observe_unary` (jit.h:415-435).
- The only emitter of `H_for_iter` passes a constant 0 as its table (eval_iter.c:243), so `h_for_iter`'s vote at helpers.c:854 never runs.
- Lines: jit.h:379-435 (57), jit.c:2136-2253 (~118), the forget code (jit.c:2286-2310, ~25) and the free code (jit.c:2374-2377).
- It still costs time on every operation. The interpreter makes out-of-line calls that reach `state_of` and return:
  - every CALL of a builtin or kernel function from warm code computes `lu_jit_kind` twice and calls `opclass_note` (ceval.c:1829-1837);
  - so do generic FOR_ITER steps (ceval.c:3697-3701), COMPARE_OP and BINARY_OP with an OTHER operand (ceval.c:3758, 3919), and every non-int UNARY_NEGATIVE/INVERT, floats included (ceval.c:3995).
- Speed effect is unmeasured. A counting build of call_tail plus the BINARY_OP and UNARY handlers on pyflakes with the JIT on would show it.
- Risk is nil, with one caveat: jit.h:166-168 claims the class tables use CALL's argument kinds, which is false today.

**b) 21 of 106 helper ids are never emitted by any pass.** I grepped `\bH_x\b` across eval*.c, opt.c, facts.c, exit.c, ssa.c and x86_64/emit.c.
- Twelve of them have bodies nothing else calls (about 170 lines with comments): `h_send` (28 lines, helpers.c:1070), `h_gen_done`, `h_get_awaitable`, `h_get_yield_from_iter`, `h_get_aiter`, `h_get_anext`, `h_push_exc_info`, `h_pop_except`, `h_reraise`, `h_seqiter_step`, `h_binary_slice_raw` (helpers.c:1576), and the `lu_float_pow` wrapper (prim/float.c:244).
- Nine ids name functions that are reached another way: `decref`, `truth`, `list_item`, `list_store` and `attr_load` are called by address from emit.c, while `field_get`, `field_set`, `unpack_exact` and `async_gen_wrap` are used directly elsewhere. Only their table rows and X() lines (~42 lines) are dead.
- Finding: these helpers are the old tier's SEND, GET_AWAITABLE, GET_YIELD_FROM_ITER and async support (23298c3e). The evaluator does not support those opcodes (`lu_ev_op_supported`, eval_ops.c:101-130). This is the retire loop luc-mopl measured at xdsl `Block.walk` (80K interpreted entries per 20 values). The capability regressed; it was not consolidated.

**c) Four of 83 IR kinds are never emitted.**
- `ENTRY_PC` (handled only in opt.c:458 and 971).
- `POW_F`: emitter case at emit.c:4202-4216, plus `lu_jit_float_pow` (helpers.c:71-116, 46 lines), which is a third copy of `prim_float_pow`'s special cases.
- `ATTR_STORE` and `GLOBAL_LOAD`: only listed in ssa.c:47-51 and facts.c:83; no emitter case exists.

**d) Orphaned functions.**
- `lu_specialize_class_value`, `lu_specialize_class_method` and `lu_specialize_instance_method` (specialize.c:407-468, ~60 lines). The JIT lost its classmethod/staticmethod-through-a-class fold with them; only eval_prims.c:457 remains.
- `lu_inline_install` (attrs.c:368, 20), `lu_frame_reserve` (frame.c:138, 13), `lu_function_template` (function.c:184, 5).

### 2. Entry into compiled code: four gates and four result handlers. ~300 lines, medium confidence.

**The job:** decide whether compiled code may run this frame now, then hand back one of the six `LU_JIT_*` results.

**The same gate is written three times:**
- `lu_jit_enter`, jit.c:1404-1409 (C, from the interpreter).
- `lu_jit_run`, jit.c:1528-1533 (C, from `lu_eval`). It adds poll-events and `disabled`.
- `direct_entry`, emit.c:5370-5567 (198 lines of machine code, from `lu_jit_direct` and unit-to-unit calls). It re-tests:
  - dead, globals and builtins (in C this is `find`'s job, jit.c:1256);
  - stack floor, instrumentation version, monitoring, tracing and the eval_frame hook;
  - depth and datastack room.
  It then re-implements `lu_frame_fill` (frame.h:311) in machine code.

**The result is handled four times:** the ceval.c:3116-3138 switch, `lu_jit_run`'s tail (jit.c:1565-1597), `lu_jit_direct` with `lu_jit_call_finish` (jit.c:1601-1635 and helpers.c:1047-1069), and the emitted `direct_done` (emit.c, 72 lines).

**Must agree:** the gate conditions, the frame layout, and the depth-before-release order. They already disagree on the last: `direct_done` gives the depth back after the unit's releases, while the interpreter gives it back before (luc-2r1l review fixes).

**Measured:**
- The entry path in jit.c is 2.4% of cycles mean (luc-orn7 note, luc-mopl).
- A direct call costs ~280 instructions. The entry stub is ~109 of them, 39 of which are "the checks the interpreter makes at a call" (luc-ne54 step 2). lt-98e0's target is ~30.
- At bare startup, `lu_jit_enter` tries plus `lu_jit_observe` are 7.5% of the JIT's added cost (luc-1rwj D).

**Design status:** docs/design/jit-architecture.md §5 lists the direct entry, `lu_jit_run` and `lu_jit_call_finish` for deletion in favour of one entry table (§4.4). §11 says that table is not built.

**Risk:** high. Walkers, generators and C entry markers all depend on these paths.

### 3. Call dispatch: four dispatchers. ~150 lines, medium confidence.

**The job:** call this callable with these arguments.

**The dispatchers:**
- The interpreter's CALL fast head (ceval.c:1846-1918) plus `call_tail` (ceval.c:1935-2135, ~200 lines).
- The JIT helpers: `call_argv` (helpers.c:1164, 58), `call_kw_argv` (1349, 29), `call_function` (981, 48), plus `h_call_bound`, `h_call_ex` and `h_call_spread` (68).
- `lu_call` and the vectorcall shims (capi/object.c:111, function.c:100).
- eval_call.c at compile time.

**Duplicated pieces:**
- The `type(x)` shortcut appears three times (ceval.c:~1876, ceval.c:1962, helpers.c:1174).
- Bound-method unpacking appears four times (ceval.c:1890, 1945; helpers.c:1183, 1360).
- The kernel-target-then-native step appears twice (ceval.c:2069-2102, helpers.c:1193-1210).

**Disagreements:**
- `call_kw_argv` has no kernel-target or `lu_native_call` path, so a kernel builtin called with keywords goes generic.
- `call_argv` has no class "instance plan" (ceval.c's `plan_for`). Class calls from compiled code go through `lu_call`, then `type.__call__` in Python, which matches the `prim_call:alloc` row.

**Argument binding** exists as `call.c bind` (145 lines, canonical), eval_call.c `bind` (114, the compile-time mirror), and exact-argument fast paths in ceval.c:1900, helpers.c:993 and `direct_entry`.

**Measured:** C time under compiled code in the call helper rows (luc-mopl rows 1, 5, 12, 46) totals 6.7% of cycles mean. Call and helper glue as leaf functions is ~9.7% (luc-mopl).

**Risk:** medium. Monitoring and `eval_frame` hook semantics differ per path.

### 4. Attribute access: one predicate copied about 20 times, and a generic path that does its own work twice. ~100 lines; highest-evidence disagreement.

**The job:** does this type use the default attribute protocol, and if so, find the attribute.

**The predicate ("same `__getattribute__`/`__setattr__` as object") is written separately in:**
- specialize.c:202 `default_getattribute`, used 7 times;
- ceval.c:779 `object_attr_hook`, the only C copy that also accepts `tp_getattro == PyObject_GenericGetAttr`;
- ceval.c:801;
- eval_attr.c:160-161 (`instance_kind`) and 588;
- descr.c:1363 and 1382;
- jit.c:1973;
- eval_call.c:953;
- the kernel: object.py:126-163 (with a `generic_wrapper` test added by stage C1) and builtins.py:114.

**Evidence of drift:**
- This is the owner's C1 bug (luc-2r1l stage C1). Fixing it took pyflakes with the JIT off from 26.66G to 14.76G instructions (−45%).
- It still disagrees today: specialize.c lacks the slot test, so classes over C generic slots never specialize. The cost is generic LOAD_ATTR at 0.5M × 490 instructions (1.7% of pyflakes, JIT off), STORE_ATTR_FAST at 0.37M × 280, and DELETE_ATTR at 0.21M × 493 (luc-2r1l stage F).

**The generic LOAD_ATTR path repeats itself.** ceval.c:3321-3416 does the module path, `attr_shortcut`, `_PyType_LookupRef` and `object_attr_hook` twice. It then calls `lu_getattr_native` (ceval.c:818), which repeats all four. That is 2-4 MRO lookups per generic load beyond what one pass would need.

**"Find this attribute" has independent implementations in:** kernel `generic_getattr`/`getattr_hook`/`lookup_attr` (object.py; `lookup_attr` repeats `getattr_hook`'s dispatch ladder), `lu_getattr_native`, `_PyObject_GenericGetAttrWithDict` (capi/internal.c:217), the specialized forms, and eval_attr.c. `h_load_attr` (helpers.c:580) calls native, then `PyObject_GetAttr`, then the kernel.

**Collapse:** one per-type protocol word (generic, type, module or custom), set wherever slots are rewritten (`lu_set_slot`, slots.c:852; `_type_set`), and read by every site above.

**Measured:** LOAD_ATTR helper rows are 2.83% + 1.03% of cycles mean, with `type_lookup` a leading leaf (luc-mopl rows 2 and 10). `type_lookup` is 2.0% of cycles mean (luc-mopl) and 6.3% of pyflakes (luc-1rwj E, not split by caller). How much of it these predicates cause is unmeasured.

**Risk:** medium. `__class__` assignment and `__bases__` changes must update the word.

### 5. Type feedback: up to four stores per site, under three keying schemes. Lines roughly neutral, but this is the measured compile cost.

| store | written by | read at build time by | key |
|---|---|---|---|
| kind words: BINARY_OP, COMPARE_OP, STORE_SUBSCR, UNPACK_SEQUENCE (jit.h:122-164) | interpreter, 4 sites | `lu_ev_feedback` (eval_arith.c:8) | code, pc |
| FOR_ITER iterator word, branch counts, CALL ran word and callee tag | interpreter | eval_iter.c:23 and 199, eval_ops.c:257 | code, pc |
| interpreter inline caches (specialize.c) | specializer | interpreter; `cached_site` (eval_attr.c:118); `h_load_global` | code, pc |
| callee tables (jit.c:2064) | interpreter only | `speculate_callee` (eval_call.c:105), only when n == 1 | code, pc |
| pics (jit.c:1915-2025) | compiled generic paths and interpreter (`pic_note_interpreted`) | `attr_ways` (eval_attr.c:529) | code, pc |
| `lu_prof` (jit.c:417-546), a linear list per root searched under a mutex | compiled code only (`H_profile`) | about 37 `lu_ev_learn_*` call sites | root plus hash of the whole inline chain |
| `jc->hot` / `outs` | `hot_exit` | `lu_ev_room` | `lu_jit_site_key`: code, pc, outer code, outer pc |
| nospec, fails, noinline | exits | `nospec()` | code, pc |
| opclasses | nothing (dead, item 1a) | nothing | |

**Where two stores describe one site:**
- FOR_ITER: the iterator word for list, tuple and range, plus `lu_prof` for generators (eval_iter.c:199 and 227).
- BINARY_OP: the kind word plus `lu_prof` (eval_arith.c:162).
- LOAD_ATTR: the inline cache, plus the pic, plus a `lu_prof` used only to own the `H_pic_same` guard (eval_attr.c:540-545).
- CALL: the callee table plus `lu_prof` (eval_call.c:1032, 1130, 1265).

**Evidence:**
- luc-mopl cause A ("profile staleness"): 18.9 pp summed over 14 programs for values 4-23 (about 1.35% of each program's cycles on average) and 14.4 pp for values 10-23, from 301 builds.
- 57-95% of steady-state learn events are a site and type already learned in another context. That is the per-chain key failing to share with the per-site truth.
- The owner's "pic learned classes the profile places did not" is the pic/`lu_prof` key mismatch.
- The class entries in callee tables are never targets (`speculate_callee` returns NULL unless the entry is a function or code). They act only as "polymorphic" markers. luc-2r1l measured that removing them costs +14.6% on `-c pass`, and did not explain why.

**Collapse:** one per-site type record with context refinement only when the site is polymorphic. luc-mopl's planned fix 1 (seed empty contexts from the site) is the minimal step toward this.

**Risk:** this is a semantic change to specialization, and kernel code's per-caller types are the reason the chain key exists (jit-architecture.md §11).

### 6. Rebuild and retirement policy (jit.c:60-95, 1121-1360, 1742-1912).

**Kill reasons:**
- learned-since-build, learned-type-fails, learned-kind-fails (`prof_exit`, jit.c:562);
- grown (jit.c:1197), retired (1225), hot-site (1742);
- writer invalidation (`lu_jit_unit_kill`, 1182);
- mark-catch, mark-raises, mark-noinline, mark-noborrow and mark-nospec, each also with an old-unit-* variant (1885-1906);
- compile failure (`escalate`).

**Counters:** `level`, `wait[]`, `fails[]`, `nospec[]`, `marked` and global `marks`, `grown`, `learned`, `stale`, `built_ins`, `hot[]`; per unit `entries`, `leaves`, `hot_calls`, `outs[].left`, `refused` and `built`; per profile `fails`, `dropped`, `unshared` and `poly`; per pic `tries` and `passes`.

**Thresholds:** 12 constants (`GUARD_FAILS`, `INS_PER_FAIL`, `LEAVES`, `WARMUP`, `MAX_LEVEL`, `INLINE_BUDGET`, `MAX_GROWN`, `HOT_CALLS_PER_INS`, `LOOP_CALLS`, `STALE_PER_INS`, `HOT_ENTRIES`, `ENTRIES_SEEN`).

**Three ways to set a wait:** `wait_at` sets (WARMUP + ncode) << level; `prof_exit` and `hot_exit` set it to 0; `state_cold` sets the threshold.

**The ledger branch** (luc-1kz0-land, 1cb5d685; diffed against master):
- It removes the `GUARD_FAILS`/`INS_PER_FAIL` trigger (`fails_enough`), `STALE_PER_INS`, `HOT_CALLS_PER_INS`, the `HOT_ENTRIES` grow trigger (`grow` and `lu_jit_grow`), and `pic->passes`.
- It keeps `retire`/`LEAVES`, `WARMUP`/`level`/`escalate`/`wait_at`, the `hot_exit` site list, the marks, and the dead opclasses.
- It adds 9 cost constants and is net +470 lines in runtime/jit.
- What remains to fold into it: retire and escalate as ledger losses, and one wait rule.

**Measured:** steady-state compile share is 4.4% mean for values 4-23 and 2.6% for 10-23. Steady-state interpreter time is about 5% (luc-mopl).

### 7. Kernel fixed-arity twins and `*args` wrappers. ~250 Python lines, medium confidence.

- There are now 17 `kernel_twin` registrations (grep of lib/kernel). jit-architecture.md §2 counted 13 and §5 lists them for deletion.
- Only the JIT uses twins (eval_call.c:509). The interpreter runs the varargs original, although kernel-customization.md:254 says the interpreter takes the twin. So two Python bodies of each builtin must agree, one per tier.
- 31 methods use `def m(self, *args, **kwargs): _noargs(...)` or `_one(...)` (str.py 23, bytes.py 6, memoryview.py 3). This exists to produce CPython's METH_NOARGS error wording.
- `builtin_style_error` (call.c:80) already produces that wording, but refuses dotted qualnames (call.c:84), which excludes methods. Extending it to methods would let these become `def upper(self, /)`.
- Measured: a cold str method is 2-3 interpreted frames at ~650 instructions per frame (luc-1rwj B).

### 8. Frame value releases: nine sites, four known order disagreements. ~15 lines deletable.

**Sites:**
- `lu_frame_release_refs` (frame.h:369, top-down, canonical) and `lu_frame_retire`;
- `frame_dealloc` (frame.c:484), whose order differs from CPython's;
- `gen_clear`;
- the forced-record cleanup in exit.c:726-743, bottom-up and commented "none in a correct unit";
- ceval's `exit_unwind`, which releases after unlink;
- in the JIT: `lu_ev_return` (eval_call.c:735-739), `throw_to` (eval_exc.c:161-166) and `raised_out` (eval_exc.c:236-241), which are three identical drop-frame loops, plus `lu_ev_leave_root`, `lu_ev_settle` (items appended last), and `direct_done`.

luc-2r1l lists disagreements (a) to (d) as open.

**Count-step variants:** the header `Py_DECREF`; `lu_decref` (lucifer.h:1310, the same body, forced inline); `lu_release_last`; `lu_try_incref_*`; the emitted `incref`, `increfs`, `decref`, `release_stub`, `release_instance` and `free_in_line`; and `lu_jit_decref`/`lu_jit_decrefs`.

**Measured:** reference counting is 15.2% of cycles mean, inside compiled code (luc-ne54). Holds by object instead of by name cut 3.1-9.3% of instructions on that branch, which is the one collapse in this area that has been built.

### 9. Small copy-paste in C.

- `index_arg` is copied 4 times: bytes.c:22, str.c:23 (the only copy with a compact-int fast path), memoryview.c:28, and deque.c:251 (which differs).
- `bool_of` is copied 7 times. `lu_kernel_target` and `lu_kernel_function_of` (descr.c:141 and 188) are near-duplicates. `h_load_method` is an alias of `h_load_attr_pic` (helpers.c:603). `H_listiter_new` and `H_tupleiter_new` share one function.
- `h_prim_words` builds a fake PyLong on the stack (`stack_int`, helpers.c:1268) so that a primitive can convert it back to a C integer (`lu_long_as_long`). Both sides appear as leaves in luc-mopl rows 18 and 39 (0.72% and 0.26%). luc-ee8q's leaf design removes this round trip.
- The evaluator picks a primitive's fold by `strcmp` on its name (30 in eval_prims.c, plus a linear words table). This is a compile cost only, unmeasured.

### 10. Native kernel forms: three implementations per builtin.

- `lu_native_call` (ceval.c:1062-1137) has native versions of dict.get, getattr, hasattr, setattr, index, bytearray.append, list.append, dict.items, len and isinstance.
- Each also has a kernel Python version and usually a JIT treatment (`class_test`, eval_call.c:1022; the eval_prims folds; `H_list_append`).
- isinstance's three versions (builtins.py:107, `isinstance_native` at ceval.c:1023, and `class_test`) agree today by hand.
- list.append runs in compiled code by two routes at once: luc-mopl row 6 (`prim_call:list_append`, 1.18%) and row 14 (`HELPER:list_append`, 0.80%).

## Ranked by measured speed effect

1. **Call and entry protocol (items 2 and 3).** The call helper rows are 6.7% of cycles mean and the jit.c entry path 2.4%. A direct call costs ~280 instructions against lt-98e0's target of ~30.
2. **Rebuild policy and feedback keying (items 5 and 6).** Steady compiles are 4.4% / 2.6%. Within that, cause A is ~1.35% per program, budget growth (B) 16.2 pp (~1.16%) and marks (E) 9.3 pp.
3. **Attribute protocol drift (item 4).** One instance has been fixed already, for −45% on pyflakes with the JIT off. What remains measured: 1.7% (generic LOAD_ATTR) plus the STORE_ATTR_FAST and DELETE_ATTR rows with the JIT off, and helper rows of 2.83% + 1.03% + 0.80% with the JIT on.
4. **Reference-count duplication (item 8).** 15.2% inside compiled code. Collapsing holds removed 3-9% of instructions on the luc-ne54 branch.
5. **A per-operation cost with no reader: no NDEBUG.** Defining it gives −3.0 to −3.3% instructions per bytecode, −0.6 to −2.4% on whole runs, and −1.1% on DeltaBlue (luc-2r1l stage F). The owner has not decided.
6. **Dead opclass hooks (item 1a).** These run on every warm-code builtin call and every generic operator, and are unmeasured.

## Not duplicated, although it looks it

- **exit.c:** one materializer (`object` and `take`) serves guard and error exits, `lu_jit_sync` and `lu_jit_force`, and all 8 exit reasons go through `lu_jit_deopt`. This is the consolidated part of the JIT.
- **Pic versus the specializer:** pic entries are made by running the interpreter's own specializer on a scratch site (jit.c:1955-1963), so their shapes agree by construction.
- **`h_prim` versus `h_prim_call`:** the same call under different effect classes (FX_SEEN versus FX_PY, which means publish, pin and poll). The difference matters, and luc-ee8q builds on it.
- **`lu_decref` versus `Py_DECREF`:** identical bodies, but the copy exists only to force inlining, which was measured at 20% of `lu_frame_pop`. It goes only if the vendored header becomes always-inline.
- **call.c `bind` versus eval_call.c `bind`:** one runs and one emits code. A shared binding plan (§4.4) is the only way to collapse them.
- **The emitted free and dealloc paths:** a guarded machine-code copy of mimalloc's fast path (the `#error` at emit.c:2473). It is needed for speed and is checked at build time.
- **LOAD_COMMON_CONSTANT:** on master both tiers now read `interp->common_consts` (ceval.c:4376, eval_ops.c:516).
- **The callee tag word (cache words 2-3):** a filter in front of the callee table so that only a change takes `pic_lock`, not a second store.
- **Options:** `LUCIFER_JIT_ONLY` is used by no tool or test (grep of tools/ and tests/), but it costs one test per compile. stats, events and debug are three separate channels for separate uses.

## Overall judgement

For what the system does, runtime/jit's 20.8k lines are reasonable: a method JIT with evaluator-based inlining, escape analysis, a deopt materializer, field-kind unboxing and a 5.8k-line x86 backend. The recoverable bloat in it is the ~600-line residue of the 11-14 rewrite (about 3%), plus a handful of near-duplicate helpers.

The complexity sits in three places, none of which is duplicated JIT machinery of the Smalltalk kind:
- **Tier policy in jit.c (~1,000 lines):** 12 thresholds, about 15 kill reasons and three feedback keying schemes. It is where the measured steady-state compile cost comes from, and the ledger branch rewrites only half of it.
- **Call and entry protocols:** four gates and four result handlers. The design doc already marked them for deletion; the plan was never finished.
- **The architectural pattern:** each operation exists as canonical kernel Python, then a C `*_native` shortcut, then interpreter specialized forms, then a JIT fold, all agreeing by hand. Today's attribute and isinstance bugs and the ~20 copies of the protocol predicate come from this. The single highest-evidence consolidation is one per-type attribute-protocol word.

A 20% delete-and-get-faster pass is not supported by the evidence. The speed lies in replacing the call and entry protocol and in the policy and feedback keying. Deleting what is dead is a cheap, safe first step, but it buys little speed by itself.
## 2026-11-25: dead leftovers deleted (items 1a-1d)

Four commits, one per group, each building on its own:
- 1a: the operator class tables (`struct lu_opclasses`, want/note/vote, forget and free code) and every interpreter hook that fed them: CALL of a builtin or kernel function from warm code, generic FOR_ITER, COMPARE_OP and BINARY_OP with an other-kind operand, non-int UNARY_NEGATIVE/INVERT. h_for_iter loses its table argument; `lu_jit_observe` returns nothing; the jit.h comment on CALL's argument kinds is corrected. LU_OPCLASS_WAYS stays: profiles use it.
- 1b: the helper ids no pass emits. Rows only for decref, truth, list_item, list_store, attr_load (called by address from emit.c), field_get, field_set, unpack_exact, async_gen_wrap (called directly); rows and bodies for seqiter_step, binary_slice_raw (with lu_slice_native_raw and bytearray_slice's machine-int mode, which only it reached) and the lu_float_pow wrapper. Kept, ids, rows and bodies, for the work restoring yield from / await / async: h_send, h_gen_done, h_get_awaitable, h_get_yield_from_iter, h_get_aiter, h_get_anext, h_push_exc_info, h_pop_except, h_reraise.
- 1c: IR kinds ENTRY_PC, POW_F (with lu_jit_float_pow and LU_JIT_POW_FAILED), ATTR_STORE, GLOBAL_LOAD (and the global_load census count only it fed).
- 1d: lu_inline_install, lu_frame_reserve, lu_function_template (make_function loses its notify flag), FX_ALLOC. Kept: lu_specialize_class_value/class_method/instance_method.

Gate: 33 kernel programs for the areas touched in jit-off, plain, jit, jit-deopt; 7 thread programs in handoff; the full jitcheck, 0 failing.

What the dead hooks cost, instructions:u, pinned, one core lease per run, two runs per arm (mean), master cb59992d against the four commits:

| workload | JIT | master | deleted | change |
|---|---|---|---|---|
| -c pass | off | 115.27M | 113.86M | -1.2% |
| -c pass | on | 139.52M | 130.67M | -6.3% |
| stdlib imports | off | 1148.3M | 1141.4M | -0.6% |
| stdlib imports | on | 1388.8M | 1334.1M | -3.9% |
| pyflakes | off | 12.83G | 12.60G | -1.8% |
| pyflakes | on | 10.06G | 9.93G | -1.2% |
| AWFY Richards 5x100 | off | 411.36G | 405.39G | -1.5% |
| AWFY Richards 5x100 | on | 60.36G | 60.35G | -0.0% |
| AWFY DeltaBlue 5x12000 | off | 74.94G | 73.73G | -1.6% |
| AWFY DeltaBlue 5x12000 | on | 25.55G | 25.47G | -0.3% |

Left: items 2-10 of the survey.
