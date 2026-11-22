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

