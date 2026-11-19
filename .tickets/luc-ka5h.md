---
id: luc-ka5h
status: closed
deps: []
links: []
created: 2026-11-19T00:05:50Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: operators dispatch on the few classes a site learned

Gap report 2026-11-18 mechanism #3. Where an operator's operand is of one of a few types at a site (None == x with x a Packet or None in Richards), the evaluator had no type to fold the kernel's compare/binary_op/getitem/contains on: the value profile (lu_prof) went poly at the second type and the site called H_compare_op -> lu_compare_native -> eq_native -> eq_as, four _PyType_Lookup a compare (Richards: type_lookup 21% + eq_native/eq_as/native_eq 12% of instructions). The old middle end had operand-class arms and H_compare_ideq; f9c8284 dropped both (ideq stayed as a dead helper).

Change: the value profile keeps up to LU_OPCLASS_WAYS types; lu_ev_kernel_arms builds one way per learned type (the instruction built again with the type known, so the kernel function folds under the class's version dependency) and a last way that reports any other type and takes the generic path. IS folds on known types (two types: two objects; NoneType/NotImplementedType/ellipsis: one), and a constant int's truth folds, so object.__eq__/__ne__ fold to identity per arm. eq_as answers by type version per thread (the generic path, interpreter and helper), replacing the dead H_compare_ideq.

## Acceptance Criteria

jitcheck lane 'operators' (tests/kernel/jit_operator_arms.py) holds helper calls flat; Richards loses the compare path's instructions on a core lease; floor quick, kernel lanes and check-jit pass.


## Result

Landed with luc-b5lx. Firing (Richards, 5 iterations, perf instructions:u): type_lookup 23.7% -> 1.2%, native_eq
4.8%, h_compare_op 3.1%, eq_native 2.8%, lu_compare_native 2.7%, eq_as 1.7% -> under 0.1% each; whole run 110.0 G
-> 64.7 G instructions. Helper calls (-X lucifer_jit_stats, 3 iterations) 227 M -> 178 M. Lane `operators`: helper
calls at 20000 / 40000 iterations, master 2.09 M / 3.33 M (62 a round), here flat.

Also in it: kernel int methods' `int_cmp(...) < 0` compares as machine ints (int_cmp's result is an int), which
the ccalls lane needed: with the folds above a dict lookup's getitem unit inlined down to int.__lt__'s compare and
called call_special out of line there (recursive) on every lookup.

Instructions on a core lease (steady per iteration; master b804b96 -> the landing, 3 reps where shown, else 1):
Richards 0.588 (cycles 0.63), Json 0.925, CD 0.996, DeltaBlue 1.014 (3 reps; 0.88-1.01 across runs), other AWFY
rows 1.000; pyperformance scimark 0.808 (3 reps), base64 0.699, raytrace 0.896, pickle_pure_python 0.953,
chaos 0.955, unpickle_pure_python 0.980, nqueens 0.980, go 0.951 (its usual swing), json_loads 1.007,
float 1.026 (accepted loss, see luc-voki), hexiom/nbody 1.00.

scimark's master instructions vary run to run (3.80 G and 5.33 G per steady iteration in different runs, base
long runs 303-319 G); the landing measured 3.07 G in all six runs, so scimark is 0.58-0.81 of master. A build
of the landing without the int_cmp type measured the same (scimark 3.07 G, float 1.030).
