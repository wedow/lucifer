---
id: luc-eog8
status: open
deps: []
links: []
created: 2026-11-22T22:46:34Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# attrs: field kinds in the class layout, so an attribute load or store is a few instructions

Measured (exact single-step counts on master, luc-bpmh's table): a compiled int attribute load is 21 instructions and a store 35; they are 84 of the 167 instructions per iteration in the read-4-attributes idiom and 70 of 141 in store-2-attributes. PyPy's whole iteration is 35 and 23.

Why: an inline attribute is self-describing (a value word that is an object, absent, or an unboxed marker, plus a raw word), so every load re-derives what it holds: the in-dict flag, the marker compare, and when the value word holds an object (anything the interpreter or C stored, which is every instance made before its __init__ was compiled) the type compare, the immortal and owner checks, the int's tag, digit and sign. A store decides the same things under the object's lock.

Mechanism: record per class and name what every instance holds there, in the shared names table: unseen, int (fits a machine word), float, or object. Then:
- For an int or float kind the raw word is always valid whoever stored it: the interpreter and C store paths write the raw word beside the object they store; compiled stores write the raw word and the marker.
- A compiled load under the class version guard it already has is a presence compare on the value word and a load of the raw word (object kind: the value word itself). A compiled store of the kind is the raw word, the marker, and a release of the old value word only if it held an object.
- A store of another kind generalizes the name's kind to object once, for good, and invalidates the units that depended on it (the same dependency machinery as other class facts). Instances stay self-describing, so nothing is rewritten.
- The in-dict flag becomes a class fact too (no instance of this class has moved its values to a dict), invalidated by the first move, instead of a test per access.
Depends on luc-6b0l stage 1 for the lock-free store sequence.

Acceptance: instructions per int attribute load <= 4 and per store <= 8 on the idiom rows read-4-attributes, store rows, monomorphic call, and the Bounce-shaped loop; instances made by the interpreter and by compiled code read at the same cost; a kernel test where one instance later stores a str into an int field (and a float, and deletes it, and moves to a dict via vars()) in all three lanes.


## 2026-11-23: design note and firing check

### What an instance holds, by name (the invariant)

The shared names table (struct lu_shared_names, runtime/object/attrs.c) gets one kind byte per name, beside the names: UNSEEN (0), INT, FLOAT, OBJECT (LU_FIELD_* in runtime/lucifer.h). The kinds only move up the lattice UNSEEN -> INT or FLOAT -> OBJECT, by compare-and-swap, never back.

- INT: an exact int (PyLong_Type itself, not bool, not a subclass) whose value fits int64 and is not INT64_MIN (the tier's int range, jit.h LU_KIND_INT). A value word of this name in any live instance of the class is NULL (absent), the LU_UNBOXED_INT marker, or such an int object, and in the last two cases the raw word holds its value.
- FLOAT: an exact float (PyFloat_Type itself); the same with LU_UNBOXED and the binary64 bits.
- OBJECT: anything; the raw word means nothing unless the value word is a marker (as today).
- bool is OBJECT: True is an int, but a bool box is a distinct object that a reader must hand back (identity, repr), and its raw word would not say which.
- A store of another kind (a str, a bool, an int past int64, a float into an INT name, an int into a FLOAT name, a subclass instance) makes the name OBJECT for good, before the value word changes.
- Once a name is OBJECT nothing writes its raw words again: an object store leaves them, and a number stored by C into an OBJECT name (lu_inline_store_i/_f's markers) is stored as its box instead. So a reader that still relies on the old kind (a unit not yet stopped by the invalidation) reads a value that was stored under that kind, never one that was not. Compiled raw stores are taken in line only under their own kind, so the one exception is such a stale unit's store, which writes a valid number of the old kind beside its marker.

### Every writer (from a read-only survey of origin/master; file:line there)

Value or raw words:
- attrs.c store_locked (616): the one place every C and interpreter store ends: lu_inline_store/_f/_i (662-676, via store_at 636), lu_attr_store (682-737: setattr, generic setattr, the kernel's instance_store, STORE_ATTR's specializations and fallbacks in interp/ceval.c 3311-3374, descr.c lu_setattr_native 1367, capi PyObject_GenericSetAttr object.c:295 and every PyObject_SetAttr form through tp_setattro, prim_instance_store type.c:2389, the JIT's helpers h_attr_store helpers.c:493 and h_store_attr 598, the deopt's instance_new exit.c:115 through GenericSetAttr). Covered: store_locked joins the kind with the value's kind first, then writes the raw word for an INT or FLOAT name before the release exchange of the value word.
- attrs.c lu_attr_store's delete (694) and move_to_dict (474): value words to NULL. Covered: NULL is absent for every kind.
- attrs.c boxed_locked (354), lu_inline_install (375), lu_inline_box_all (750): replace a marker by its box, raw word unchanged and equal to the box. Covered: the invariant holds for the box.
- attrs.c lu_inline_clear (781): words to NULL. Covered.
- Compiled stores (emit.c OP_INLINE_STORE 3985, inline_store_raw for _I/_F 3181; emitted by eval_attr.c store_as 651 and lu_ev_instance_real 283): the IR builder knows the class and index, and reads the name's kind at compile time. In line: an object store only when the kind is OBJECT (it stays OBJECT); INLINE_STORE_I only when the kind is INT, INLINE_STORE_F only when FLOAT. Anything else carries LU_STORE_BY_C in imm2 and the emitter jumps to its existing C slow path. Anything else (UNSEEN, or a kind the store would generalize) takes the store's C path, store_locked, which joins the kind. A kind read at compile time can only have moved toward OBJECT, and every in-line choice above stays valid when it does, so no unit depends on a kind in stage (a).
- Object creation (PyType_GenericAlloc type.c:680, _PyObject_GC_New gc.c:483, emit_new_object): value words zeroed: absent.
- Not writers: copy, pickle and __setstate__ go through setattr; specialize.c only caches offsets; imagegen does not hold instances.

Liveness and the in-dict flag (stage d's class fact):
- move_to_dict (attrs.c:470) is the only place that sets LU_ATTRS_IN_DICT; it is reached from detach_inline (497) for lu_instance_dict (535: vars(), obj.__dict__, _PyObject_GetDictPtr type.c:598, PyObject_GenericGetDict type.c:610, pickle's _getstate_default through prim_instance_dict), lu_instance_set_dict (556: assigning or deleting __dict__), set_class (type.c:2416: __class__ assignment), and lu_attr_store (708: no room, or a full order record). Stage (d)'s class fact is invalidated inside move_to_dict, before the flag is published.

Readers of the raw word: boxed_locked and lu_inline_install (under the lock), and the compiled INLINE_LOAD_I/_F (emit.c 2894-2900, after the marker check, no lock).

Verifier: lu_inline_kinds_check(obj), cheap (one pass over the words): every word of an INT or FLOAT name is absent, the right marker, or an exact object of the kind whose value equals the raw word. Exposed to tests as _lucifer.inline_kinds_ok, called by the stage (a) test after each writer.

### Readers that need the object

The first reader that needs a box makes it and installs it in the value word (boxed_locked, as today). An OBJECT-kind load stays the value word load followed by a separate reference step, so that luc-5bi7's borrowed-reference hook is one line on that step.

### Free-threading

- A kind changes by compare-and-swap under the store's own lock or critical section (the lock is per object; kinds are per class, hence the atomic).
- A store that generalizes: the CAS to OBJECT, then (stage b) the invalidation of the units relying on the name's kind, then the value word's release exchange. A compiled load on another thread that passed its version guard before the invalidation reached it reads the value word (present) and the raw word: the raw word still holds the last value of the old kind, a value that was stored there, as if the load ran before the generalizing store. Units stop at their next liveness check.
- A raw word is written before its value word (store_locked's relaxed raw store, then the release exchange; the compiled store's raw word, then the marker), and a compiled load reads the value word, then the raw word: with x86's ordering of loads, a reader that sees the new value word sees the raw word written with it or later.
- The torn read the survey suspects (a word switching between an int marker and a float marker under a lock-free reader) cannot happen for a named kind: a name is INT or FLOAT, never both, and a store of the other number generalizes it to OBJECT, whose loads read the value word.

### Plain-release bit (luc-uqyj design)

Lives in the class flags, not the names table: its inputs (finalizer, base, slots, layout) are class slots, not per-name facts, and its events (lu_set_slot, __bases__ assignment) are not the names table's. Built separately after stage (d).

### Stages

(a) kinds recorded and kept by every writer, compiled stores choosing in line or C by kind, the verifier and a kernel test across every writer above; no reader uses a kind. (b) loads: under the version guard, an INT or FLOAT name is a presence compare on the value word and the raw word's load (a dependency on the kind, invalidated by generalization). (c) stores by kind. (d) the class-level in-dict fact.

## 2026-11-23: stage (a) numbers

Exact instructions per inner iteration (single-stepped), origin/master 161b0abb -> stage (a): loads 162 -> 162, stores 130 -> 147, pstores 109 -> 108, fstores 104 -> 104, calls 104 -> 104. The stores row's 17 are the raw word an object store of an int now writes beside it (two stores an iteration: the type, one-digit and sign tests and the raw store); stage (c) replaces those stores. Gate: the focused kernel programs (48, among them attr_field_kinds and the attribute, class-value, unboxed-field and release programs) pass in plain, jit and jit-deopt; jitcheck 0 failing.

## State (keep current)

Branch luc-eog8 from origin/master 161b0abb.

Done, committed (one commit, "attrs: record what each class's instances hold under each name"): stage (a). Kinds recorded per name (struct lu_shared_names gains `kinds`, one byte per name after the names: runtime/object/attrs.c), joined by CAS in store_locked (every C and interpreter store ends there) before the raw word (int/float names) and then the value word are written; a marker stored by C into a name that became OBJECT is stored as its box. Compiled stores: eval_attr.c store_kind() sets LU_STORE_BY_C (ssa.h, imm2 bit 32) unless the name's kind takes the store in line (object store: OBJECT; INLINE_STORE_I: INT; _F: FLOAT); emit.c jumps straight to the existing C slow path for those (INLINE_STORE and inline_store_raw), and fresh_word masks imm2 with LU_STORE_WORDS. Verifier lu_inline_kinds_ok (attrs.c), exposed as _lucifer.inline_kinds_ok (runtime/prim/type.c). Test tests/kernel/attr_field_kinds.py (passes on master too: the verifier is new; it catches a writer that leaves a raw word stale).

Gate for (a): .scratch/gate.sh (focused kernel programs in plain, jit, jit-deopt, then jitcheck lanes incl. releases): all pass but pickle_c_module, which needs the _pickle accelerator the bin/lucifer + build/lib/stamp build does not make (environmental; dropped from the list).

Also in stage (a): compiled object stores into an INT (FLOAT) name take an exact one-digit int (exact float) in line and write its raw word before the value word (emit.c OP_INLINE_STORE, LU_STORE_INT_RAW / LU_STORE_FLOAT_RAW in ssa.h); a store whose owner's class the IR does not know finds it by lu_type_of_version (runtime/prim/type.c: a version-to-type table like CPython's _PyType_LookupByVersion, borrowed, a slot per version modulo 4096, cleared when a class leaves its version or dies). Without these the stores row went to C (130 -> 697 instructions).

Next step: report stage (a) and wait for review; then stage (b) below.

Then stage (b): compiled INT/FLOAT loads under the version guard read the value word (present?) and the raw word, with a dependency on the name's kind invalidated by join_kind's move to OBJECT (the dependency machinery in runtime/jit/deps.c, LU_DEP_*), the invalidation before the value word's store.

Scratch tools (untracked, .scratch/):
- keep/k.py and keep/kk.py: the idiom rows (python3-runnable; `bin/lucifer k.py NAME N`).
- keep/tr.sh NAMES (env LUC=binary OUT=dir): exact single-step traces with gdb (trace_gdb.py), per IR opcode and C function; run under the host lock (test class); summarize with keep/trsum.py dir/NAME.json.
- keep/ab.sh NAMES (env A=, B=): interleaved min-of-3 cycles and instructions per iteration; run under the host lock (core class), pinned.
- keep/ctr.sh ROW BIN...: frontend/branch counters; under the host lock (core class).
- keep/dis.sh ROW FUNC + keep/ann.py: annotated disassembly of a function's last unit.
- gate.sh: the focused gate; sa.sh: build + gate + traces for stage (a).

Not obvious from the code: lu_jit LU_KIND_* (jit.h) is a different, unrelated enum (call-site operand kinds); the field kinds are LU_FIELD_*. INT kind excludes INT64_MIN to match the tier's int range. emit_new_object no longer zeroes raw words (luc-uqyj), so a raw word means something only beside a present value word.
