---
id: luc-qtqf
status: open
deps: []
links: []
created: 2026-11-24T03:01:59Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit: descriptor and special call shapes in compiled code (property, classmethod, staticmethod, super, with)

From the first full idiom table (daily 2026-11-23; cycles per iteration, Lucifer / PyPy / ratio), the call and attribute shapes that still leave compiled code:

| idiom | Lucifer | PyPy | ratio | vs CPython |
|---|---|---|---|---|
| property read and write | 1467 | 10 | 154x | 2.7x slower |
| classmethod call | 494 | 5 | 95x | 1.1x slower |
| with statement | 498 | 5 | 105x | about equal |
| *args call | 306 | 5 | 64x | 0.5x |
| super() call | 316 | 7 | 47x | 0.6x |
| staticmethod call | 183 | 5 | 38x | 0.5x |
| global read and write | 230 | 6 | 36x | 1.1x slower |
| nonlocal read and write | 108 | 9 | 12x | 0.4x |
| getattr/hasattr with a constant name | 103 / 128 | 10 / 8 | 11x / 17x | 0.4x / 0.6x |
| reversed() iteration | 92 | 6 | 16x | 0.4x |
| __slots__ read and write | 87 | 10 | 9x | 0.6x |

Each is an ordinary language feature whose protocol is a descriptor or special-method call: a property's fget/fset is a Python function reached through the property object's __get__/__set__; classmethod and staticmethod objects are wrappers whose target is known under the class version guard; zero-argument super() resolves through __class__ and the MRO, constant under the same guard; with calls __enter__/__exit__ looked up on the type; a global store goes through the module dict.

Mechanism: under the class version guard the site already holds, resolve the descriptor at compile time and bind the call to the underlying function as an ordinary bound call (inlined or direct): property get and set to fget/fset(self[, value]); classmethod to f(cls, ...); staticmethod to f(...); super().m(...) to the function found past __class__ in the MRO; with to the type's __enter__ and __exit__, with the exception path of __exit__ kept as today's until exceptions are cheap. Global stores as a store into the dict's known entry under the dict's keys dependency.

Firing check first on each row's IR dump (what leaves, through which helper), and on three real programs for how often each shape occurs.
Acceptance: each row within 3x of PyPy; rows slower than CPython today (property, classmethod, global writes) first.


## 2026-11-24: firing check and counts

Exact instructions per inner iteration on origin/master (single-stepped, the idiom rows of tools/bench/idiom_cases.py), with what leaves compiled code:

| idiom | per iteration | in C | through |
|---|---|---|---|
| property_rw | 3878 | 3718 | HELPER:store_attr: no specialization for a store through a property, so the generic setattr finds the property and calls the setter from C; the read is bound already (LOAD_ATTR_PROPERTY, fget in line) |
| classcall | 1515 | 1231 | HELPER:load_method (M.cm: the classmethod reached through the generic method load, a bound method made) and HELPER:load_attr (cls.k inside cm: a class attribute read through the generic path) |
| staticcall | 551 | 469 | HELPER:load_method (M.st: the staticmethod through the generic method load) |
| supercall | 923 | 620 | HELPER:super_attr (super().val: the lookup past __class__ in C and a bound method made each time) |
| with_stmt | 1455 | 983 | HELPER:load_special, twice (__enter__ and __exit__ as bound methods made each time) |
| global_rw | 676 | 539 | HELPER:store_global (lu_dict_replace, the dict's watch event and lu_jit_invalidate on every store) and HELPER:load_global (the store took the cached entry back) |
| nonlocal_rw | 362 | 157 | HELPER:cell_store (under the cell's critical section) |
| getattr_const | 400 a call | 151 | prim_call getattr (lu_attr_lookup, lu_inline_index: the constant name looked up each time) |
| hasattr_const | 480 a call | 243 | the same, through hasattr |
| slots_rw | 211 | 51 | HELPER:slot_store |

How often the shapes run in real programs (pyperformance benchmarks run once each on the oracle after a warm run; calls of property getters and setters, classmethod and staticmethod functions, __enter__/__exit__; executed STORE_GLOBAL, LOAD_SUPER_ATTR, LOAD_SPECIAL): bm_logging property get 20, LOAD_SPECIAL 80 (40 with statements); bm_pathlib staticmethod 7003, LOAD_SUPER_ATTR 6000, classmethod 4, property get 4; bm_richards_super LOAD_SUPER_ATTR 65796; bm_go STORE_GLOBAL 102460; bm_raytrace, bm_deepcopy and bm_chaos none of these to speak of (bm_pprint timed out under opcode tracing). So by real use: super() calls, global stores and staticmethod calls first; property stores, classmethod calls and with statements are rare in these programs (property reads are common but already bound).

## 2026-11-24: first family, property stores

The site's pic (jit.c lu_jit_pic_note) records a store through an exact property whose setter is a Python function (LU_PIC_STORE_PROPERTY, the property as the entry's descriptor; properties are deferred, as inline caches require). store_as guards the class version and the property's fset field, then calls the setter: in line when it is the site's only way (lu_ev_call_discard: inline_call with LU_RETURN_DISCARD, whose return now drops the result), out of line (H_call) as one of several. A property subclass, fset None, and anything else keep the generic store. Test jit_property_set: the setter replaced by setter() and by a new property, the property deleted, a property on a base replaced, fset None (the interpreter's AttributeError), a property subclass with an instance dict entry of the same name, __setattr__ defined on the class later, __class__ assigned to a class with another setter, a metaclass property, a setter that raises. property_rw: 3878 -> 155 instructions per iteration, 1723 -> 63 cycles (one core lease; PyPy 10, so 6.3x: the 3x target is not met yet: the store of _v inside the setter 35, the list item 21, the counts 20, the two setter and getter guards 10).

## 2026-11-24: review of property stores: the setattr protocol

The fold recorded a store through a property whenever the specializer left STORE_ATTR alone, for any owner, so a class with its own __setattr__ had it skipped (and a __setattr__ that raises, as frozen classes have, let the store through). Fixed (26a45ba5): the entry requires tp_setattro to be PyObject_GenericSetAttr and object's __setattr__ to be the one in the class's MRO, the specializer's own condition for stores; a type as owner (a metaclass property) keeps the generic store, by choice (type's setattro runs type_modified and the class's own rules). The read path that was in line before (LOAD_ATTR_PROPERTY) is the interpreter specializer's, which already requires the default __getattribute__ and no __getattr__ (runtime/interp/specialize.c). Every family below has the same rule: the binding holds only where the type's attribute slot is the generic one. Tests added to jit_property_set (the first fails on 2e6f8443): __setattr__ from the start and one that raises, __setattr__ added and kept for a whole hot loop, property.__init__ re-called in place, a setter raising inside try/finally with the traceback's frames, setters returning fresh objects with finalizer probes on the stored value and the dropped result, the setter replaced during its own run, generator and async setters (the never-awaited warning filtered: its timing differs across carriers), a site mixing property and plain stores.

## 2026-11-24: super()

LOAD_SUPER_ATTR is resolved at compile time (eval_ops.c super_known) when super is the builtin, the class is a known class (the __class__ cell's value, guarded) and self's class is known: from a type fact, from the version a guard on self already holds (lu_type_of_version), or speculated to be the class that defines the method (a type guard; a site whose guards keep failing goes generic). The attribute found past the class in self's class's MRO is constant under that class's version (a change to any class in the MRO takes the version back): the method form becomes the function and self, a value with no __get__ becomes the value, and neither a super object nor a bound method is made. A function read as an attribute (not called), a classmethod's super (self a class) and anything with a __get__ keep the generic helper. Test jit_super_calls: method calls, super().__init__ with keywords in constructors, attribute reads (a plain value, a function as a bound method, a property, two-argument super, super(B, self)), a classmethod's super(), and the MRO changing mid-loop (a method added to an intermediate class, the base's replaced, __bases__ assigned, self's __class__ assigned).

## 2026-11-24: global stores, why and the design

Today: a compiled global read folds the value as a constant with a dependency on that dict entry's value (eval_attr.c lu_ev_load_global, lu_ev_dict_const); STORE_GLOBAL is the helper lu_ns_store, which replaces the entry's value under the dict's lock (lu_dict_replace) and sends the watch event (lu_dict_send_event), and the event invalidates every unit that folded that value (lu_jit_invalidate, LU_DEP_VALUE). A global stored in a hot loop therefore kills the units reading it on every store; after the failures the read site turns to the H_load_global helper (nohoist), and the store stays a C call with a lock and an event: global_rw 676 instructions per iteration, 539 in C.

Design: a global stored hot is read and written as the dict entry's value slot. The dict's keys version (the entry's index stays while the keys do) is the dependency of both, not the value: a compiled read loads the entry's value word (one load, the dict and index constant), a compiled store exchanges it and releases the old value (the dict's lock where the dict may be shared, as compiled list stores do; none while serial). The watch event and the invalidation are still due to units that folded the value: a store sends the event only when some unit depends on the value (a per-dict count of value dependents, kept by the dependency table), so a global that only the slot readers read is stored with no event. Which reads fold: a global stored once at import and read afterwards keeps today's fold; a site whose store side ran hot (the existing nohoist flag on the entry) reads the slot. nonlocal stores (cell_store) are the same on a cell: the cell's word read and written in line, the cell's lock where it may be shared. Not built yet.

## 2026-11-24: super() numbers

supercall: 923 -> 67 instructions per iteration (all compiled; no helper). Whole-run instructions, min of 5, on a core lease: the supercall row driver (3000 outer iterations) 862.9M -> 606.3M; richards_super (pyperformance's, 20 runs of Richards.run(1), pyperf removed) 4237M -> 3091M (-27%). In richards_super the super() calls are in methods compiled on their own, where the __class__ cell is the root frame's (not a constant) and self's class is unknown: the class comes from the root function's closure (guarded) and self's class is speculated to be the defining class (a type guard). Before those two it moved 1.4%.

## 2026-11-24: review of super(): fixes

A plain value found past the class (no __get__ on its class at compile time) was folded under self's class's version only; its own class gaining __get__ later left the unit returning it unbound (CPython calls __get__). The fold now also relies on the value's class (lu_ev_class_relied under its version); functions need nothing more (their class is the immutable function type). Other compile-time "no __get__ / __set__" decisions in this ticket: the property-store fold names an exact property (its class is property itself, immutable), so nothing more is needed there. Self's class, when not known, is now the one the site's profile learned (lu_ev_learn_type: one class, a type guard; none yet or several, a profiling helper and the generic way), not the class defining the method, so a base method called with subclass receivers keeps its fold where the site sees one receiver class. Polymorphic receivers (a different next class per receiver) stay generic: per-class arms (as type_ways does for operators) are the follow-up. The property-store entry reads the class's version tag before the setattr and MRO checks, so a __setattr__ added meanwhile takes the tag back. Tests added to jit_super_calls: a plain value whose class gains __get__ (fails on 2a0aa418), a foreign self, self rebound before super(), the __class__ cell rebound, super shadowed by a global late in the loop, a diamond and a base method with several subclass receivers, a name set on an intermediate class mid-loop, super().__init__ reaching object.__init__ with an argument.

## 2026-11-24: super(): the held case, and the class-value fold under it

The held case ("get gains __get__") was never the interpreter: with -X lucifer_jit=off it passes on master and on the super() build alike, and the kernelcheck "plain" lane runs with the JIT on. The build that showed it failing had been compiled before the review fix it was meant to carry (a rebuild of the same source passes it in plain, jit, jit-deopt and handoff:2; with the jit's debug log the store `K.__get__ = ...` now reports the K version dependency killing S.get's unit). A case for the suspected hole after the store was added (the store compiled in the loop, the read right after it in the same iteration): it passes, as facts.c turns a dependency guard reached after an instruction that may run code into a liveness check.

The same rule was missing on master, outside super(): class_attribute (eval_attr.c), the fold of a plain value read off a known class (H.x), checked that the value's class had no __get__ at compile time but relied only on the versions of the class and its metaclass, so K gaining __get__ (or __get__ with __set__) mid-loop left H.x returning K unbound, with the JIT on only (plain, jit and jit-deopt lanes; passes with it off). Fixed with one helper, lu_ev_plain_value (eval_prims.c): the value's class has no __get__ and is relied on under its version; class_attribute and super() both use it. Test class_value_gains_descriptor: a class value whose class gains and loses __get__, __get__ with __set__, __set__ alone and __delete__ alone, read through an instance with and without its own entry, the class, super() and getattr (fails on master in the class-read rows only).

Numbers (whole-run instructions, min of 5, one core lease per arm, against master 7dfa1306): richards_super 4239M -> 3158M (-25.5%); the supercall row driver 875.6M -> 619.0M (-29%). The supercall trace: 67 instructions per iteration, all compiled.

## State (keep current)

On master: property stores with both review fixes (test jit_property_set). On branch luc-qtqf-super, based on master 7dfa1306, not landed: the class-value fix with its test (first commit) and super() with its review fixes squashed (second commit). Gate on that tree: 22 focused kernel programs pass in plain, jit and jit-deopt (jit_super_calls, jit_property_set, class_value_gains_descriptor, jit_bound_calls, jit_cells, jit_class_kwcalls, jit_isinstance_fold, jit_field_kinds, attr_field_kinds, jit_user_handlers, jit_class_values, descriptors, super_lookup, jit_super_attrs, jit_super, err_super_missing, err_super_type, type_attr_cache, jit_attr_changes, class_attr_writes, staticmethod_access, jit_poly_attrs), and the super, class-value and type-attribute thread programs in handoff and handoff:2.

Not done in this ticket, in order of real use (counts above): global stores (the design above: the dict entry's value slot under the keys dependency; the watch event only where a unit folded the value; nonlocal cells the same), staticmethod and classmethod calls from the class (M.st(...), M.cm(...): the generic method load; the instance case is already resolved by lu_specialize_instance_method; the class case needs the same under the class's own version, guarding the metaclass has no __getattribute__ or data descriptor of the name), with (__enter__/__exit__ as bound calls on the type, the normal exit in line, the exception path as today), then getattr/hasattr with a constant name and slots stores. Each binding holds only where the type's attribute slot is the generic one. super() with polymorphic receivers (a different next class per receiver) stays generic; per-class arms are the follow-up.

Not obvious from the code: an inlined call's result reaches the caller at the return join, after the calling instruction is built, so an instruction that pushes nothing must inline with LU_RETURN_DISCARD rather than pop after the call. Squash with an explicit base (git merge-base), never against a moving origin/master. A binding of an attribute protocol (property, super, descriptor) holds only where the type's attribute slot is the generic one, and a value folded because its class lacks __get__ must rely on that class's version too (lu_ev_plain_value). Check that a binary was built from the source it is said to carry before diagnosing from it. The task output file of a background job contains the script it was given, so wait on markers the script prints, not on words in its source.

Scratch tools (untracked, .scratch/): keep/ic.py (the idiom rows for the tracer: SCRIPT=ic.py keep/tr.sh NAMES, env LUC= OUT=, test class); rs/row.py NAME N (an idiom row without the tracer's signal) and rs/richards_super.py N (pyperformance's, pyperf removed); rsrun.sh ARMS (whole-run instructions, min of 5, both programs, core class); census/shapes2.py with census/shim/pyperf.py (the oracle's shape counts on a pyperformance benchmark); g2.sh (build and the focused gate); diag/ (the reproducers); bin/lucifer-m (master 7dfa1306), bin/lucifer-q2 (this branch).
