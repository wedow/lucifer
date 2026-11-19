---
id: luc-ye9l
status: open
deps: []
links: []
created: 2026-11-19T09:03:54Z
type: task
priority: 2
---
# array item sites that meet several typecodes, or ints and floats for a float array, take the primitive

After luc-cxwo, typed_get/typed_set go in line only where the site (per root, per inline chain) met one typecode and one value type: lu_ev_learn_const relies on a value profile with one entry. A function used on arrays of several typecodes (tests/kernel/jit_typed_items.py's shared sites) or one storing both ints and floats into an array('d') falls back to h_prim_call. Split the instruction per learned value the way lu_ev_kernel_arms splits per learned type (value ways up to LU_OPCLASS_WAYS), and an int-or-float value with both_ways (eval_arith.c). Measure on a generic numeric helper over several arrays.

