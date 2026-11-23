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

## 2026-11-23: review of stage (a), and stage (b)

Fixes from the review of stage (a):
- E1: a C store wrote the raw word by the value's kind, not the name's after the join, so an int stored into a float name overwrote the float's raw word while a compiled reader could have seen the float marker (a torn read: a number made of the other kind's bits). Now the raw word is written only when the joined kind equals the value's, int or float: a name's raw words only ever hold its first kind. Rule, restated: no C writer writes a raw word of a name that left int or float, or of an object name; a compiled store built under the old kind writes only a complete old-kind pair until its version guard stops it.
- E2: the box a marker needs under a name that no longer takes it was made after note_insert recorded the index, so a failed allocation left a recorded index with an absent word, and retries appended again. The kind join and the box now come first, before the lock (take_kind), so store_locked cannot fail after note_insert for that reason.
- Kinds now: UNSEEN, INT, FLOAT, OBJECT (the first value was some other object: no word ever holds a marker), ANY (an int or float name that met another kind: markers of its first kind may remain in old instances). OBJECT and ANY are final. A name that leaves INT or FLOAT calls lu_type_modified on its class after the join and before any value of another kind is stored; that takes the version back, which kills every unit that relied on the version (proven and hoisted guards register LU_DEP_TYPE) and signals running tasks, and fails every unhoisted guard. take_kind runs before the object's lock, so lu_type_modified is never called under an object lock or a serial unlocked section.
- INT64_MIN: one rule, accepted under int (value_kind no longer excludes it). The raw-word loads represent it; only the boxed-int unbox path's wide fallback uses INT64_MIN as its failure value, and that path reads the object, not the raw word. With every int64 a valid raw int, a marker has no value to check.
- lu_type_of_version: the fixed 4096-slot table (a collision lost a class, and its stores went to C) is now a growable open-addressing map in the interpreter state (struct lu_version_types), heap types only, under its own leaf lock; the lookup takes a reference with a try-incref under that lock, and a dying type's dealloc removes it under the same lock before the memory is freed. Chosen over carrying the class with the specialization because STORE_ATTR's inline cache (CPython's layout, four code units) has no room for a pointer, so carrying it means a side structure per site written by the specializer; the map stays beside set_version, where versions are assigned and taken back.

Stage (b): INLINE_LOAD's imm2 carries the name's kind under the version guard (LU_LOAD_KIND, bits 60 up; the class value below). INLINE_LOAD_I/_F under its own kind (and no origin object) is the in-dict test, the presence compare on the value word, and the raw word's load. An object load under an OBJECT name has no marker compare and no out-of-line box path; the reference step (incref_owned_or_exit / owned_or_exit) stays separate. ANY and unknown kinds keep today's code. The in-dict test stays until stage (d): the words are cleared after the dict is published and the flag set, so a reader in that window could read a value the dict has since replaced; dropping the test needs the class-level fact.

## 2026-11-23: stage (b) and (c) numbers

Exact instructions per inner iteration (single-stepped), origin/master 161b0abb -> (a) -> (b) -> (c): loads 162 -> 162 -> 94 -> 94; calls 104 -> 104 -> 88 -> 88; stores 130 -> 147 -> 147 -> 147; pstores 109 -> 108 -> - -> 108; fstores 104 -> 104 -> - -> 90; oo (the Bounce-shaped loop, mean of 11 traced iterations, data dependent) 2905 -> - -> 2689 -> 2526. An int load is 5 instructions (in-dict test and branch, presence compare and branch, the raw word straight into the value's register), from about 23; a float store 6 when the word holds the marker. ≤4 per load needs the in-dict test gone (stage d). The stores row (an object, the loop variable, into an int name) still makes the raw word from the box and keeps the reference counting: identity of a named int is kept, so the box is stored.

while_attr: 2405 per iteration on master and on every stage, 2158 of them in C (lu_jit_deopt, eval_frame, lu_jit_enter, type lookups): its Node class is made anew per call, so the unit's version guard fails and the loop leaves and re-enters every iteration. Not a field-kind question; a master behaviour worth its own ticket.

## 2026-11-23: loads check the word (review of b and c: F1, F2)

F1 (a thread that lost the race to generalize stored at once, before the winner's version change killed anything) and F2 (a unit killed from another thread runs to its next poll, reading raw words never stored) both came from loads trusting the kind. Loads now check the word they read, so nothing depends on when an invalidation arrives, and generalizing takes nothing back (take_kind no longer calls lu_type_modified: it would only kill units that are correct anyway).

Kinds: UNSEEN, INT, FLOAT, OBJECT (first value some other object), ANY_INT, ANY_FLOAT (took anything after an int or a float first). The first number kind is for good. Raw-word rule: every store of a value of the name's first number kind (an exact int that fits int64; an exact float) writes the raw word, before the value word, whatever the name's kind is now; no other store writes it (a compiled object store under an ANY name writes the raw word back unchanged). So a raw word only ever holds values of the first kind (E1 stays closed), and every value word that is the first kind's marker or an object of the first kind has its value in the raw word.

A compiled int load (float likewise) under a name whose first number is int, by the word it reads:

| word | int load | float load |
|---|---|---|
| NULL (absent) | leaves | leaves |
| the first kind's marker | raw word (hot: compare in memory, branch, load: 3 instructions) | raw word, the same |
| exact int of one digit (tag below 2 << _PyLong_NON_SIZE_BITS) / exact float | raw word (cold: about 9) | raw word (cold) |
| exact int wider than one digit | read from the object, as before (ownership checked, lu_jit_unbox_wide; leaves when it does not fit int64) | - |
| any other object (a str, a bool, an int subclass, the other kind's box) | leaves at the load's guard, which the site counts | leaves |

The compact test is the cheapest exact one: one compare of lv_tag, and every compact int fits, so its raw word was written. The type and tag are read without a reference: a box freed meanwhile stays readable until the thread's next quiescent point, and whatever it reads as only picks the raw word, which holds a value stored under the name. An object load under an OBJECT name keeps no marker test (no marker can exist there); under an ANY name it keeps it.

Stores: a raw compiled store is in line under a name whose first number is its own, a complete pair even after the name generalized. An object store under an int-first (float-first) name writes the raw word for a one-digit int (an exact float) in line; a wider int goes to C; any other value goes to C while the name is still INT (FLOAT), which generalizes it, and is stored in line without a raw word once it is ANY.

## 2026-11-23: stage (d) without a class-level fact

The in-dict test is not needed by a load or store that finds a value in the word: move_to_dict leaves every word NULL for good (under the object's lock, after publishing the dict and setting the flag), and an index below the class's count is in every live instance's room. So a load that finds the marker or an object reads a word still in line, and one that finds NULL leaves (the class-value path tests the flag on NULL, as it did). The test never closed the race with a move on another thread either: test, then move, then read gives the same old value as no test. Stores need the flag only for an absent word, where they would add a name; a word holding a value under the store's lock (or while serial) is in line. So no fact and no invalidation: nothing for a class whose instances move routinely to pay, and no unit that must avoid depending on it again. The int load is 3 instructions, a float store over a marker 4.

## 2026-11-23: what would remove the stores row's counting

The stores row stores the loop variable, an int a local also names, into an int name. The box must be the stored value (identity), so the store increfs the new box and decrefs the old: about 25 instructions of 37. Number origins (the borrowed-reference work's stage 3: knowing that an int's only other name dies before anyone could compare identities) would let the store write the marker and the raw word instead.

## 2026-11-23: numbers after the self-checking loads and stage (d)

Exact instructions per inner iteration, origin/master 161b0abb -> (b) -> (c) -> now: loads 162 -> 94 -> 94 -> 120; calls 104 -> 88 -> 88 -> 97; oo (mean of 11 traced iterations) 2905 -> 2689 -> 2526 -> 2400; fstores 104 -> - -> 90 -> 89; stores 130 -> 147 -> 147 -> 148. A load of a word holding the marker is 3 instructions (compare in memory, branch, raw load: fstores' float load, disassembled); a float store over a marker 4 (load, compare, branch, store). The loads and calls rows read instances the interpreter made, whose words hold int boxes: those take the cold path, about 12 instructions a load (type and tag checks, then the raw word) where (b) trusted the kind in 5.

## State (keep current)

Branch luc-eog8 from origin/master 161b0abb, history rebuilt without local details (old head c7d9e06c, tree identical). Commits: 21e1e2c5 stage (a); e78a3906 review fixes of (a); a0ce0a7d stage (b); 0187a775 stage (c); 6d830451 ticket; a7b2acf8 ticket luc-2dd7 (classes made at run time); a533dff5 loads check the word (F1, F2); then stage (d), the in-dict test only for absent words. Gated after each of the last two: 50 focused programs in plain, jit, jit-deopt; jitcheck 0 failing; the kinds programs and jit_threads in handoff and handoff:2.

Nothing running. Next step: report and wait for review.

Known limits: the parallel (locked) raw store keeps the general code; instances the interpreter made read through the cold path (above); the stores row keeps its counting (above).

Scratch tools (untracked, .scratch/):
- keep/k.py and keep/kk.py: the idiom rows (with oo, the Bounce-shaped loop); `bin/lucifer k.py NAME N`.
- keep/tr.sh NAMES (env LUC=binary OUT=dir, ALL=1 to break on every entry's last build, needed for while_attr): exact single-step traces with gdb (trace_gdb.py); under the host lock (test class); keep/trsum.py dir/NAME.json summarizes.
- keep/ab.sh NAMES (env A=, B=): interleaved min-of-3 cycles and instructions; under the host lock (core class).
- keep/ctr.sh ROW BIN...: counters; under the host lock (core class).
- keep/dis.sh ROW FUNC + keep/ann.py: annotated disassembly of a function's last unit (dis3.sh: oo, dis4.sh: fstores).
- gate.sh: the focused gate; sb.sh / sc.sh / sd.sh: build + gate + traces per stage; wa/: the while_attr probe.
- bin/lucifer-base (master 161b0abb), bin/lucifer-a, -b, -c, -d (self-checking loads), -d2 (stage d): the arms.

Not obvious from the code: lu_jit LU_KIND_* (jit.h) is an unrelated enum (call-site operand kinds); the field kinds are LU_FIELD_*. emit_new_object does not zero raw words (luc-uqyj), so a raw word means something only beside a value word of the first kind. The cold stream follows the hot one, so a jump to the label right after a cold block is a jump to the next hot instruction: leave it out. tk in a worktree names tickets after the directory (le-*): rename to luc-*.
