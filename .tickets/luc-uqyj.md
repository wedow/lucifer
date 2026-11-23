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
