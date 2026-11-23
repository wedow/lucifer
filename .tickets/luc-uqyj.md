---
id: luc-uqyj
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: allocation and release in line for common shapes

Measured: lu_object_new, lu_gc_alloc, lu_object_free, _Py_Dealloc, the per-type deallocs and lu_mem_* are about 10% of real-program steady state; an instance made and stored into a list costs 190 cycles (PyPy 23), of which subtype_dealloc, lu_inline_clear, lu_clear_weakrefs and lu_object_free are 30% and HELPER new_object 11%; xs[j] = xs[j] + 1 is 118 cycles, most of it making and releasing the int.

Mechanisms: the release of an object whose class has no finalizer, no weak references and inline attributes only is a short in-line sequence (release each attribute, return the block), chosen under the class guard the unit already holds; int, float and tuple releases the same; the allocation fast path covers every size class the inline path can pop. A list or tuple of ints or floats holding them unboxed is a larger change with a C-API consequence (C reads ob_item directly), to design only if the table still shows it after this.

Acceptance: idiom rows object-kept-in-list <= 60 cycles, list update <= 40.


## 2026-11-23: measured (origin/master f2577f26)

Executed instructions per inner iteration, exact (single-stepped steady-state iterations, C attributed to the function it ran in and to the IR instruction that called it). "cold" is the unit's out-of-line code. floatstore is a new row: xs[j] = j * 0.5 + i over a list of 100 floats. isinst and genexp are per item of their inner loop.

| row | total | compiled | C | allocation (compiled) | release |
|---|---|---|---|---|---|
| alloc_escape | 488 | 252 | 236 | HELPER new_object 48 in line, BOX_INT 9, INLINE_STORE 50 | old instance: LIST_STORE 38 cold + subtype_dealloc 68, lu_inline_clear 56, lu_object_free 48, _Py_Dealloc 25, lu_clear_weakrefs 22, lu_jit_decref 12, _Py_MergeZeroLocalRefcount 5 = 274 |
| listidx | 284 | 218 | 66 | BOX_INT 32 | old int: LIST_STORE 38 cold + _Py_Dealloc 25, long_dealloc 24, lu_jit_decref 12, merge 5 = 104 |
| floatstore | 242 | 180 | 62 | BOX_FLOAT 21 | old float: LIST_STORE 38 cold + _Py_Dealloc 25, float_dealloc 20, lu_jit_decref 12, merge 5 = 100 |
| isinst | 565 | 383 | 182 | HELPER build_tuple 53 | tuple: tuple_dealloc 72, _Py_Dealloc 25, lu_jit_decref 12, merge 5 = 114; prim_is_subtype 62 |
| strfmt | 1264 | 176 | 1088 | lu_object_new 61, PyTuple_New 51, PyUnicode_New 45 | tuple_dealloc 68, _Py_Dealloc 50, lu_object_free 38, str_dealloc 18, lu_jit_decref 12, merge 10; the formatting itself (format_decimal 180, join_items 169, copy_into 123) |
| genexp | 406 | 212 | 194 | PyLong_FromLongLong 19 (in lu_long_add) | seqiter and int add in C: lu_seqiter_next 56, lu_long_add 40, PyCriticalSection 45 |

Reading: on the rows the ticket names, the release is most of the cost: 274 of 488 instructions for an instance, 104 of 284 for an int, 100 of 242 for a float, 114 of 565 for a tuple. Every release of an object whose count compiled code drops to zero goes lu_jit_decref -> _Py_MergeZeroLocalRefcount -> _Py_Dealloc -> the type's dealloc, through the decref's slow path in cold code (38 instructions there before the call: publishing the state for a finalizer that ints, floats and tuples of them cannot have). genexp's cost is the generator protocol and the int add in C, not allocation: out of this ticket.

## 2026-11-23: stage 1, release in line

When compiled code drops the last local reference to an object this thread owns with no shared references, the release's slow path now first tries one stub the unit shares (object in RDX): exact compact ints and exact floats go back to their freelists; exact tuples of 1 to 20 items none of which dies here go back to theirs; exact compact ascii strs and plain instances (lu_subtype_dealloc, no tp_finalize, object as base, no __slots__, no dict, no weak reference to them, every inline value unboxed or not dying, the type not dying, the collector's count short of a flush) go to mi_free with the unit's registers kept around the call. Anything else, or allocators or a ref tracer observed, takes the C path as before.

Skipped from the generic path, and why that is sound: _Py_DecRef/_Py_MergeZeroLocalRefcount (the count is the owner's and the shared count is zero, so nothing to merge and nobody to queue for); _Py_Dealloc's trash queue (nothing released here releases anything that dies, so no dealloc nests); the publication of the frame state for a finalizer (none of these kinds runs code); lu_clear_weakrefs (the list is empty or the type has none); lu_inline_clear and the type's decref (their counts drop in place, none reaches zero). The collector's tracked bit is cleared before the block goes, as _Py_Dealloc clears it, and the collector cannot run meanwhile (no poll, no park between the checks and the free).

Exact instructions per iteration, and cycles (interleaved min of 3, origin/master vs now, one lease; alloc_escape three leases):

| row | instructions before | after | in C before | after | cycles before | after |
|---|---|---|---|---|---|---|
| alloc_escape | 488 | 410 | 236 | 23 (mi_free) | 206, 200, 193 | 170, 167, 166 |
| listidx | 284 | 221 | 66 | 0 | 128 | 104 |
| floatstore | 242 | 184 | 62 | 0 | 109 | 94 |
| isinst (per item) | 565 | 496 | 182 | 68 | 190 | 138 |
| strfmt | 1264 | 1238 | 1088 | 1013 | 859 | 665 |
| loads, tupunpack, poly (neighbours) | 162, 125, 468 | same | | | 40, 28, 128 | 39, 28, 117 |

Code bytes per unit grow by the stub (about 900 bytes once per unit that releases): alloc_escape 11077 -> 13057, listidx 9640 -> 11616.

Found on the way, filed as lu-ex68: a tuple compiled code keeps virtual releases its items first to last, CPython last to first (on master too).

Left for alloc_escape (target 60): the stub's instance path is about 160 instructions (checks, the scans, the volatile saves around mi_free); the list store's lock (lock elision's), new_object's 48 and the inline stores' 50 (stage 2).

## 2026-11-23: stage 2 (items 1-2), boxes and new_object

Ints and floats are none of the collector's: lu_object_new counts only GC types, and the box's in-line path counts nothing (its alloc_count is -X lucifer_jit_stats only), so there was no accounting to amortize. The box now falls through on a freelist hit (the page pop and the negative sign moved out of line), writes the tag as an immediate and the digit from the value's own register, and leaves the object in RCX instead of moving it: BOX_INT 32 -> 29 on listidx (window NOPs included), the floor for a freelist pop and _Py_NewReference's five header words. new_object zeroes only the inline value words (a raw word is read only under its value word's marker, written after it) besides the preheader, slots and order record, takes the type's reference without a NULL test and with the thread id it already holds, and leaves the result unchecked on its in-line path: 48 -> 41 on alloc_escape.

| row | instructions after stage 1 | after | cycles (one lease, min of 3) |
|---|---|---|---|
| alloc_escape | 410 | 401 | 170 -> 162 |
| listidx | 221 | 220 | 100 -> 99 |
| floatstore | 184 | 184 | 93 -> 94 |
| isinst | 496 | 496 | 149 -> 147 |
| loads, tupunpack | 162, 125 | same | 41 -> 35, 25 -> 28 |

New rows, traced once on this build (instructions per inner iteration):
- tupret (a, b = pair(j), pair too large to inline): 64, all compiled: the tuple stays virtual across the call's return.
- shortlist (for v in [i, j, 3]): 53 for most iterations, 256 every fourth (the list's array from the allocator).
- smalldict ({'a': i, 'b': j}; d['b']): 1484, 1191 in C: lu_dict_insert 185, probe_keys 171, dict_dealloc 111, probe_unicode 107, new_keys 68, lu_dict_find_lockfree 67, h_build_map 66.
- boundm (m = b.get; m()): 995, 700 in C: type_lookup 88, lu_getattr_native 80, lu_attr_lookup 68, lu_gc_alloc 57, lu_inline_index 53, lu_fields_clear 51, lu_object_free 48, lu_object_new 45, PyMethod_New 37: the bound method is made and freed in C every time.
- strslice (out[j] = text[a:b]): 752, 478 in C: slice_unpack 96, PyUnicode_FromKindAndData 78, lu_object_new 61, lu_slice_native 55.

## 2026-11-23: stage 2 item 3, mimalloc's free fast path in line

A dying instance's or str's block of a page of this thread's segment that is neither full nor holds aligned blocks goes on the page's local free list in line, with the used count dropped, as mi_free's fast path puts it (this build encodes no free lists and keeps no padding or statistics); another thread's segment, a full or aligned page, or the page's last block (which retires) still calls mi_free. Each kind goes from the stub's first type test to its own code with one owner check (the second dispatch and the dealloc recheck are gone).

| row | instructions after items 1-2 | after | cycles (min of 3, one lease) |
|---|---|---|---|
| alloc_escape | 401 (23 in mi_free) | 371 (none in C) | 155 -> 153 |
| strfmt | 1238 | 1207 | 640 -> 636 |
| strslice | 752 | 724 | 341 -> 333 |
| listidx | 220 | 218 | 102 -> 97 |
| loads, tupunpack | 162, 125 | same | 40 -> 42, 31 -> 26 |

## 2026-11-23: review fixes for stage 1 (D1, D2)

D1: one object in several of the slots a release drops (a tuple (x, x), two attributes on one value, an attribute holding the instance's own class) passed a per-slot "count above one" test and was decremented to zero without being freed. The rule now: every object a release drops references to without freeing must be immortal or this thread's with a local count above the number of references the release can drop at most (a tuple's size; an instance's room plus one for its type). However the slots repeat, none reaches zero; anything else takes the generic path, which frees as it goes.

D2: the instance release read inline words while the attributes were in a dict (__class__ assigned, then del __dict__: the dict pointer NULL, LU_ATTRS_IN_DICT set for good) and sized the scan by the class's count of names. It now requires LU_ATTRS_IN_DICT clear and scans the instance's own room (its flags, else the class table's capacity, as lu_inline_clear reads it). Every other layout the stub reads from the class (basic size, preheader, inline-values flag, weak list offset) __class__ assignment keeps equal (compatible_for_assignment), and an assignment always moves inline values to a dict.

Also: the shared-word test says why the weak-list test depends on it; a freed str or instance gets local count 0 as every other path leaves it; releases that call C are counted (-X lucifer_jit_stats C paths "release") and a jitcheck lane (releases, tests/kernel/jit_release_paths.py) requires they do not grow with iterations, so a stub that stops firing fails. tests/kernel/jit_release_shapes.py covers D1 (tuple, attributes, own class, a float three times), D2, a dict made real, __slots__, a builtin's subclass, an attribute dying with its instance, a class dying with its last instance, tuples longer than 20 and (), an object another thread referenced, and more releases than a freelist keeps.

## 2026-11-23: what remains, by owner (exact instructions per inner iteration, before the D1/D2 fix adds a few to the stub)

alloc_escape, 371: release of the old instance (the stub: kind dispatch, owner check, class conditions, room scan and drops of the attribute words, type and collector counts, the in-line free) 168, this ticket; new_object 41 and BOX_INT 9, this ticket; LIST_STORE 38 (the list's lock byte taken and released around the store), lock elision; INLINE_STORE 50 (two attribute stores on the fresh instance), the field-kinds ticket luc-eog8; the loop, poll and guards about 45.
listidx, 218: release of the old int 41, this ticket; BOX_INT 29, this ticket; LIST_STORE 40, lock elision; DECREF 30 and LIST_ITEM 23 and UNBOX_INT 13 (reference counts and boxes around the item), luc-5bi7 and luc-eog8; the loop about 40.

Against PyPy's whole iteration (instructions, one lease): alloc_escape 36, listidx 21, floatstore 18, isinst 24, strfmt 500. Allocation plus release now: alloc_escape about 220 (6x PyPy's iteration), listidx 70 (3.3x), floatstore about 59 (3.3x): the bar (2x) is not met. The next levers by number: the instance release's 168 (most of it the class and room checks, which a per-class "plain" bit kept by the type machinery would fold into one test, and the attribute scans), then the box's header words.

## 2026-11-23: design, a per-class "plain release" bit

Goal: the instance release tests one class bit plus the object's own words instead of walking the class (today: tp_dealloc, tp_finalize, tp_base, the class's size for __slots__, the weak-list offset, the managed-dict and inline-values flags: about 25 instructions before any attribute is read).

The bit (a Lucifer-private tp_flags bit, LU_TPFLAGS_PLAIN_RELEASE): set on a heap class whose instances lu_subtype_dealloc would release by clearing inline values and the dict pointer and freeing the block, with nothing else to run: tp_dealloc is lu_subtype_dealloc; tp_finalize and tp_del NULL; tp_base is object (a deeper chain of plain classes could qualify later, each level with no slots); Py_SIZE(class) 0 (no __slots__ members); Py_TPFLAGS_INLINE_VALUES and Py_TPFLAGS_MANAGED_DICT set. Weak references are left out of the bit on purpose: a weak reference is made per object, and making one sets the object's shared-word maybe-weakref bit, which the release's shared-word test already rejects; the weak-list offset only says where the list would be.

Computed by one function, plain_release(tp), and stored with the flags, at every event that can change an input:
- Class creation: after type_new_layout and the slot fill (the first lu_set_slot pass), where tp_dealloc, tp_base, the size and the layout flags are final.
- tp_finalize / tp_del changing: every slot write goes through lu_set_slot (runtime/types/slots.c), which kernel Python calls for the class and each subclass when a dunder is assigned or deleted on a class or its bases (C.__del__ = f, del C.__del__, a base gaining __del__). Recompute in lu_set_slot after the write when the slot is tp_finalize (or tp_dealloc), for the class being written; the kernel's walk over subclasses brings each its own call, so a subclass needs nothing of its own. Unverified, to check before building: that the kernel calls lu_set_slot for every subclass when a base's __del__ changes, and that the only other writers of tp_finalize are inheritance at class creation (LU_COPY(tp_finalize) in runtime/prim/type.c, before the bit is first computed) and generator types (runtime/interp/gen.c, not heap classes).
- __bases__ assignment (prim_type_set_bases): tp_base changes; recompute after it. compatible_for_assignment keeps the size, preheader and inline-values flag, so only the base test can change; the MRO change that follows also re-fills slots through lu_set_slot.
- __slots__ cannot change after creation, and Py_SIZE of a class never changes; tp_dealloc of a heap class is set once at creation (and by a metaclass's C at its own creation).
- __class__ assignment on an instance changes no class; the instance's values move to a dict (LU_ATTRS_IN_DICT), which the release reads from the object.
- Clearing: never needed between those events; the bit only ever describes the class as it is after the last of them, and the release reads it under the same conditions it reads tp_finalize today (no other thread can be inside a slot write for this object's class while this thread holds the object's last reference and calls nothing).

The release then: kind dispatch (tp_flags & LU_TPFLAGS_PLAIN_RELEASE), owner check, LU_ATTRS_IN_DICT clear and the dict pointer NULL in the object, the room from the object's flags (capacity from the class table when 0), the scans, the frees: the class walk's 25 instructions become 3. A test for each event: a class gaining and losing __del__ mid-loop, its base gaining __del__, __bases__ reassigned to a class with a finalizer, a subclass of a plain class (bit computed for it alone), and a class whose metaclass defines tp_dealloc.

Follow-up with the plain bit (from the second review): the instance bound counts the room, not the object words present, so an instance the interpreter made (room up to 30) needs every attribute value's local count above 31 and seldom takes the fast path. Count the object words in the scan (the words above LU_UNBOXED_INT) and require each value's local count above that count plus one for the type; the D1 argument holds unchanged, since no object can be dropped more often than the object words plus the type.

## 2026-11-23: second review items

Rebased onto origin/master 032cec77 (the HELPER error exit is now `catchable`; emit_new_object's slow path jumps to the same label as before). emit.c refuses to build when the mimalloc configuration encodes free lists, pads or guards blocks, tracks them for a sanitizer, keeps statistics or is a debug or secure build (free_in_line pushes plain next pointers). jit_release_shapes.py's cases now observe what they exercise: the dict-free values' finalizers before any collection (count and distinct), resident memory across two million floats held three times (a leak of them would be 64 MB), every class freed once its instances are, each finalizer of another thread's objects once, the class's count growth between n and 2n, a class whose table is fixed (36 literal attributes: room bits 0, capacity read), more instance releases with nothing allocated between them than the collector's count batches, and tracemalloc started mid-loop. lu-r6wr closed as the same delayed dict release CPython makes.
