---
id: luc-c997
status: open
deps: []
links: []
created: 2026-11-25T11:31:03Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-ia92
---
# one attribute-protocol word per type

Item 4 of luc-ia92. The question "which attribute protocol does this type use" is answered separately in about twenty places (the specializer, the generic interpreter paths, the native C lookups, the kernel's Python hooks, the tier's folds), and the copies disagree: classes over an inherited C generic slot get no specialized attribute forms; the kernel hooks grew until a hot unit hit its inline budget (tomli_loads +10.6%); the design below also names probable bugs D2, D4, D6 and D7 found by reading, each needing its failing test before the stage that fixes it. One per-type word, decided with the slot in one function, read by every site. Stages 1 to 5 below land separately; stage 1 introduces the word, switches the specializer and makes every other site check its own answer against the word, so disagreements surface in the test lanes before any other site is switched. Each stage gets a read-only review; stages touching slots, versions or threads an adversarial one.


## Design (2026-11-25)


Read from origin/master at 48ef26dd. All file:line references are to that tree; the survey in `.tickets/luc-ia92.md` cites an older tree, so its line numbers are shifted. Nothing was built or run: every "bug" below is an inference from reading and needs the test named for it.

## 0. Summary

- **The word.** Three 3-bit fields (read, store, delete) in the Lucifer-private high bits of `tp_flags`, next to `LU_TPFLAGS_PLAIN_RELEASE` (`runtime/lucifer.h:421`).
- **Its definition.** The word is a pure function of the type's attribute slot and, when that slot is the kernel's thunk, of what the thunk's hook would find in the MRO. One function in `runtime/types/slots.c` computes it, and the same function chooses the slot in `lu_set_slot`.
- **Its ordering.** It is written together with the slot, between the two version clears that C1's review established. Cached readers read it inside the version bracket.
- **What stage 1 buys.** The specializer reads the word, so classes over an inherited C generic slot (ast nodes) get specialized LOAD_ATTR and STORE_ATTR forms. The other sites keep their predicates and check them against the word.
- **Found while reading (inference, not run).** Five places where today's copies are probably wrong, not just slow: D2, D3, D4, D6, D7 in section 1.

## 1. Inventory

How slots are decided today:
- `attribute_thunk` (`runtime/types/slots.c:272-290`) with `keeps_object_hook` (263-270) gives a class `PyObject_GenericGetAttr` when it finds object's `__getattribute__` and no `__getattr__`. Otherwise it gives the C function of a base's slot wrapper (`lu_wrapper_specific`, `runtime/types/wrappers.c:792-804`) when there is no `__getattr__`, else the thunk `lu_slot_tp_getattro`.
- The write slot is `PyObject_GenericSetAttr` only when both `__setattr__` and `__delattr__` are object's, a wrapped C function only when both wrap the same one, else the thunk.
- `object` keeps the thunk (`slots.c:866`), and static types inherit it (`inherit_slots`, `runtime/prim/type.c:1160-1169`).
- So the thunk alone means any of: generic (object, int, list, every static kernel type), type's, module's, or custom Python. That ambiguity is why every site below re-derives the answer.

### Read side

| # | Site | Predicate | What it does with the answer | Where it differs |
|---|---|---|---|---|
| R1 | `runtime/interp/specialize.c:202-205` `default_getattribute`, called at 306 (instance shape), 454 (instance method) with object, and 254, 410, 434 with type | MRO lookup of `__getattribute__` identical to the base's | Instance forms, or class forms | Lookup only: a class that finds a C wrapper of the generic function (ast) fails (**D1**, the known one). Ignores `__getattr__` on purpose, except line 376. Evaluated before the version is read at 309 (**D3**). |
| R2 | `specialize.c:453, 475, 491` | Slot is the thunk or the generic function | Gate before R1 | Passes classes R1 then refuses. |
| R3 | `specialize.c:376` | No `__getattr__` in the MRO | Allows LOAD_ATTR_PROPERTY | Per-form copy of the fallback test. |
| R4 | `runtime/interp/ceval.c:753-760` `object_attr_hook`, used at 773, 806, 1005, 3327, 3337 | Slot is `PyObject_GenericGetAttr`, or lookup equals object's | Native data-descriptor get, the native lookup, the isinstance negative | Only C copy with the slot clause. Lookup clause fails for a wrapper-generic class that has `__getattr__` (thunk slot); the kernel treats that as generic with fallback (D5, speed only). |
| R5 | `ceval.c:775-776` in `attr_shortcut` | Metaclass lookup of `__getattribute__` equals type's | Answers `Cls.name` in C | Ignores metaclass `__getattr__` (harmless: it only answers found names). |
| R6 | `ceval.c:3309-3310` and 3367 | Slot is thunk or generic | Enters the native path; picks kernel entry `getattr` or `generic_getattr` | A custom Python `__getattribute__` class also enters and pays every lookup below before reaching the kernel. |
| R7 | `ceval.c:3344` | No `__getattr__` | Runs a Python data-descriptor getter as a frame | Third copy of the fallback test. |
| R8 | `ceval.c:800` and 3298 | `PyModule_CheckExact` | Reads `md_dict` directly | See modules below. |
| R9 | `runtime/jit/eval_attr.c:160-162` `instance_kind` | Lookups: `__getattribute__` and `__setattr__` are object's, no `__getattr__` | Keeps an instance virtual | Lookup only; does not test `__delattr__`. |
| R10 | `eval_attr.c:588-589` `class_attribute` | Metaclass has no `__getattr__` and type's `__getattribute__` | Folds `Cls.name` to a constant | Stricter than R5. |
| R11 | `runtime/jit/eval_call.c:955-966` `known_subclass` | Slot is the generic function, else lookups of `__class__` and `__getattribute__` equal object's | Folds a negative isinstance | Mirrors R4. |
| R12 | `lib/kernel/object.py:123-147` `getattr_hook` | Ladder: lookups of both dunders; identity with object's, type's (and the object is a type), module's (and a subtype test); `generic_wrapper` with the type; with `__getattr__` present, `generic_wrapper` without the subtype test | Dispatch | The reference semantics (CPython's `slot_tp_getattr_hook`). |
| R13 | `object.py:155-173` `lookup_attr` | The same ladder again, for the no-`__getattr__` cases | Suppressed lookups for hasattr and getattr with a default | Second copy of R12. |
| R14 | `lib/kernel/builtins.py:104, 114` | Lookups of `__class__` and `__getattribute__` equal object's | isinstance negative | Lookup only (no slot clause, unlike R4 and R11). |
| R15 | `runtime/capi/object.c:283-292` `PyObject_GenericGetAttr` | None of its own; calls `lu_getattr_native` (`ceval.c:792-837`) | C-API generic get | **D4**: `lu_getattr_native` answers what getattr would, not what the generic algorithm would. For a type owner it runs type's lookup (`attr_shortcut`), and for a module it reads `md_dict` before type-level data descriptors. |
| R16 | `runtime/capi/object.c:206-221` `PyObject_GetAttr`; `runtime/jit/helpers.c:539-550` `h_load_attr` | Calls the slot; the helper tries `lu_getattr_native` first | | Correct. For static builtin instances the slot is the thunk, so C callers always enter kernel Python. |

### Write side

| # | Site | Predicate | Use | Where it differs |
|---|---|---|---|---|
| W1 | `specialize.c:523, 539` | Lookup of `__setattr__` equals object's | STORE_ATTR forms | Lookup only (D1). Ignoring `__delattr__` is correct for stores. |
| W2 | `runtime/object/descr.c:1376-1382` `lu_setattr_native` | Slot generic, or thunk with `__setattr__` lookup equal to object's | Native store and delete | **D2**: called with a NULL value for deletes (`ceval.c:3450`). With only `__delattr__` overridden the slot is the thunk and `__setattr__` is object's, so a delete of a `__slots__` member or C getset runs `lu_descr_set_native` (1385) and bypasses `__delattr__`. |
| W3 | `descr.c:1363` `type_setattr_native` | Metaclass lookup of `__setattr__` equals type's | Native class-attribute store | |
| W4 | `ceval.c:3444-3445, 3463` | Slot is thunk or generic | Native path; kernel entry | As R6. |
| W5 | `runtime/jit/jit.c:2253-2254` | Slot is `PyObject_GenericSetAttr` and lookup equals object's | Property-setter pic entry | Excludes every thunk type. |
| W6 | `object.py:176-191` `setattr_hook`, `delattr_hook` | `generic_wrapper` on the dunder found, else call it | Dispatch | Set and delete decided separately. |
| W7 | `object.py:112-113` in `generic_setattr` | Both lookups equal object's | Chooses the "no `__dict__` for setting new attributes" message | **D6**: CPython tests the slot. A class over a C generic slot gets a different message here. |
| W8 | `runtime/capi/object.c:294-304` `PyObject_GenericSetAttr` | Calls `lu_setattr_native` | | Same shape as D4. |

`runtime/object/descr.c:1626-1636` `prim_generic_wrapper` is the primitive behind R12, R13 and W6.

### Attribute access on a class
- The metaclass's slot is the thunk.
- R5, R10 and R12 each test "metaclass's `__getattribute__` is type's" on their own. The specializer's class forms use R1 with type as the base. W3 tests the store side.
- A metaclass that overrides `__getattribute__` fails all of them and reaches `call_special`. A metaclass that only adds `__getattr__` passes R1 and R5 but not R10 or R12's direct branch.

### Modules
- PEP 562's `__getattr__` lives in the module's dict; `_module_getattr` reads it at `lib/kernel/module.py:26-37`.
- C sites handle only exact modules, by reading `md_dict` first: `ceval.c:3298`, `ceval.c:800`, `specialize_module` at `specialize.c:230-243`, `eval_attr.c:618`. A miss goes to the kernel, which runs the generic lookup and then the dict's hook.
- A module subclass goes to the kernel ladder's module branch (R12), or to custom if the subclass defines either hook.

### Other disagreements (inference)
- **D3.** The specializer evaluates its predicate at `specialize.c:306` before `analyze` reads the version at 309 (likewise 254 before 257, and 523 and 539 before `analyze`). A hook assigned between the two leaves a cache entry under a fresh tag with the old protocol.
- **D7.** `update_slots` reads the MRO and writes the slot with no lock ("slot writers hold no lock of their own", `prim/type.c:790-793`). Two threads assigning hooks on one class can leave the slot at the earlier decision.
- **D8.** `type.__flags__` (`prim/type.c:2092`) and `PyType_GetFlags` (`capi/object.c:451`) expose bit 40 today.

### The generic LOAD_ATTR does its work twice
In `ceval.c:3287-3386`:
1. Module probe (3298).
2. One MRO lookup of the name (3314).
3. `attr_shortcut` (3315): up to four lookups for a class owner.
4. `object_attr_hook` at 3327 and 3337: two lookups each for a thunk type.
5. `lu_getattr_native` (3359), which repeats the module probe (800), `attr_shortcut` (803), `object_attr_hook` (806) and the name lookup (809).

A plain instance pays 2 name lookups; an instance of a static builtin type pays about 8 lookups; a class with a Python `__getattribute__` pays all of them and then calls the kernel anyway.

## 2. The word

### Values
One enum, used by all three fields:

```
enum { LU_ATTR_UNSET = 0, LU_ATTR_GENERIC = 1, LU_ATTR_TYPE = 2, LU_ATTR_CUSTOM = 3,
       LU_ATTR_SLOT = 4, LU_ATTR_MODULE = 5, LU_ATTR_GENERIC_GETATTR = 6 };
```

| Value | Meaning |
|---|---|
| GENERIC | object's algorithm (`generic_getattr` or `generic_setattr`), nothing else. |
| GENERIC_GETATTR (read only) | object's lookup, then the type's `__getattr__` on AttributeError. |
| TYPE | type's algorithm (`_type_getattr`, `_type_set`); the instance is a class. |
| MODULE (read only) | generic lookup, then the module dict's `__getattr__`. |
| CUSTOM | the kernel hook calls the dunder it finds (a Python `__getattribute__`, with or without `__getattr__`; a kernel type's own, such as weakref proxies or generic aliases; a copied wrapper, which then raises its TypeError). |
| SLOT | the slot is a foreign C function: call it. Includes the old-slot stand-ins. |
| UNSET | not classified yet: kernel boot, or a type that never reached the decision function. |

Fields:
- read: bits 41-43
- store: bits 44-46
- delete: bits 47-49

Set and delete can differ: one C slot serves both, but `__setattr__` and `__delattr__` are overridden independently (the slot is then the thunk, with store CUSTOM and delete GENERIC or the reverse). D2 is what happens without the distinction.

### What stays out of the word
- Managed dict and inline values are already `tp_flags` bits, fixed by layout.
- `tp_dictoffset` and `__slots__` members are layout.
- "A data descriptor exists for this name" is per name, and belongs to the version-keyed caches.
- "`__class__` is object's" (R4, R11, R14) is a per-name fact that is not a slot dunder. It stays a cached lookup next to the word.

### Where it lives
- There is no Lucifer type extension. `PyTypeObject` is CPython's layout, because extensions define static ones.
- Recommended: the private high bits of `tp_flags`, following `LU_TPFLAGS_PLAIN_RELEASE`.
  - The attribute paths already load `tp_flags` for the dict and inline tests (`ceval.c:817`, `specialize.c:310`).
  - Static extension types start with zero there, which is UNSET.
  - New heap types copy only named bits from the base (`prim/type.c:1722-1727`).
- Consequences, all in stage 1:
  - The plain read-modify-write at `runtime/object/attrs.c:209` becomes an atomic or.
  - The word is stored with one compare-and-swap loop.
  - `type_layout` and `PyType_GetFlags` mask bits 40 and up (fixes D8).
- Alternative if a foreign writer of `tp_flags` on a ready type turns up: `tp_cache`, which is unused in this runtime and marked unused by CPython (`include/cpython/object.h:220`).

### Relation to the slots
The slots stay what C calls. The definition:

- **Read word** = classify(type, `tp_getattro`):
  - `PyObject_GenericGetAttr` → GENERIC.
  - The thunk → what `getattr_hook` would do, from two lookups:
    - No `__getattr__`:
      - object's `__getattribute__`, or a wrapper passing the `lu_wrapper_specific` test with the generic function → GENERIC.
      - type's, and the type is a subtype of type → TYPE.
      - module's, and a subtype of its descriptor's type → MODULE.
      - anything else → CUSTOM.
    - With `__getattr__`:
      - object's, or a generic wrapper without the subtype test (CPython's quirk, kept at `object.py:139`) → GENERIC_GETATTR.
      - anything else → CUSTOM.
  - Any other function → SLOT.
- **Store and delete words** = classify(type, `tp_setattro`, dunder):
  - `PyObject_GenericSetAttr` → GENERIC for both.
  - The thunk → per dunder: object's or a generic wrapper with the subtype test → GENERIC; type's and a subtype of type → TYPE; else CUSTOM.
  - Any other function → SLOT.

`attribute_thunk` is rewritten on top of the same classification:
- GENERIC gives the generic function (object itself keeps its thunk, as now).
- CUSTOM with no `__getattr__` and a non-NULL `lu_wrapper_specific` gives that function (the word is then SLOT).
- Everything else keeps the thunk.

The C1 rules fall out of this:
- The stand-ins are never specific (`wrappers.c:800-802`), so a Python subclass of an old-slot type is thunk plus CUSTOM, and its hook calls the stand-in, which runs generic access.
- Copied wrappers fail the subtype test, so they are CUSTOM and raise.

`keeps_object_hook` and `prim_generic_wrapper` become dead.

### Accessor
`lu_attr_read(tp)`, `lu_attr_store(tp)`, `lu_attr_delete(tp)` are inline in `lucifer.h`: one relaxed load, shift, mask. UNSET takes an out-of-line path that classifies without storing: kernel boot fills static dicts as it goes, so nothing can be stored yet. After boot no ready type is UNSET, and a straggler is only slower.

## 3. Where it is computed, and what recomputes it

One function, `lu_attr_refresh(tp)` in `slots.c`, under a new static mutex `attr_lock`, which also covers the slot decision and write. It runs only MRO lookups, no Python. Serializing decide-and-write means the last writer sees every earlier dict store, as `plain_lock` argues for its bit; this closes D7.

Callers:

| Event | Path today | Change |
|---|---|---|
| Class creation, including type() over a C base | `lib/kernel/type.py:463-464`, then `prim_update_slots` (`slots.c:947-979`), then `lu_set_slot` case 1 | `lu_set_slot` refreshes after writing either attribute slot. |
| Assigning or deleting any of the four dunders on a class | `_type_set` (`type.py:539-553`) and `_remro_slots` (556-559), for the class and every subclass | Same hook in `lu_set_slot`. |
| `__bases__` assignment, custom `mro()` | `_set_bases` and `_remro` (`type.py:311-332`) | Same. |
| Kernel static types | Bodies installed during boot; dicts change until the last module | One pass over `lu_static_types` at `runtime/interp/frozen.c:129`, just before `specializing` is set. About a thousand uncached lookups: an estimate, to be measured on `-c pass`. |
| `PyType_Ready` of an extension type | `ready_type` (`prim/type.c:1287-1475`) | Refresh before `set_version` at 1467. |
| `PyType_FromSpec` | Spec slots written straight into the type | Refresh at `runtime/capi/typespec.c:502`, beside `lu_type_refresh_plain`. |
| `PyType_Modified` from C | `prim/type.c:405-409` | Refresh the type and its subclasses from their current slots, then clear versions. |
| `__class__` assignment on an instance | `set_class` (`prim/type.c:2552-2580`) | Nothing. Confirmed: every specialized form compares the tag of the current type on each execution (`ceval.c:3163, 3198, 3222, 3249, 3396`), and units that relied on the old type die at 2574-2575. |

### What bounds staleness
- **Extension pokes a slot or `tp_dict` and calls `PyType_Modified`.** The word is re-derived from the slot. A `__getattr__` added under a generic slot is ignored, as in CPython, which does not recompute slots there either.
- **Extension writes a slot with no `PyType_Modified`.** The word is stale until the next refresh. CPython's specializer has the same hole. Uncached C readers that trust the word instead of the slot are newly exposed (question 3).

### Ordering against the version tag
The writer's order stays as C1's review set it (`type.py:549-553`):
1. Store into the dict (which clears the tag).
2. Rewrite slots; the word is written here, inside `lu_set_slot`, with a release CAS.
3. Clear the tag again.

Cached readers (specializer, pic, JIT) must read the word inside the version bracket: load or assign the tag, then read the word and do the lookups, then re-check the tag. Then:
- A tag read before step 3 dies at step 3, whatever word it was paired with.
- A tag assigned after step 3 is read before the word, so the word is the new one.
- A new word with an old tag is harmless: the tag is dead.

The JIT needs nothing new. A unit that folds on the word depends on the type's version (`LU_DEP_TYPE`), and the ticket scheme in `runtime/jit/deps.c` already stops publication when a writer's clear overlaps the compile.

### Free threading
- Uncached readers load the word relaxed.
- A racing reader sees the old or the new word, never a mixture (one CAS).
- Either is an access linearized before or after the hook assignment, which has not returned yet. That is exactly what a reader of `tp_getattro` sees today.
- Between the dict store and the slot rewrite, the MRO shows the new hook while slot and word are old. A finalizer run in that window uses the old protocol; that is true today for slot readers.

## 4. Readers after the change

### Specializer (`specialize.c`)
- Delete `default_getattribute` (202-205), the slot gates (453, 475, 491), the two store lookups (523, 539) and the `__getattr__` lookup (376).
- `analyze` returns the word with the version, so the read is inside the bracket (fixes D3).
- Forms by word:

| Word | Forms |
|---|---|
| Read GENERIC | All instance forms. |
| Read GENERIC_GETATTR | The same except LOAD_ATTR_PROPERTY: the found-attribute forms never raise AttributeError, and unset slots and missing inline values already fall to the generic path. |
| Metaclass read TYPE | The two class forms. A metaclass with only `__getattr__` becomes CUSTOM and loses them (question 4). |
| Exact module | LOAD_ATTR_MODULE, unchanged. |
| Store GENERIC | The three store forms. |

- New: a class whose word is GENERIC through an inherited C slot gets LOAD_ATTR_INSTANCE_VALUE, LOAD_ATTR_WITH_HINT, LOAD_ATTR_SLOT, the method and non-descriptor forms, and the store forms.
- The pics follow for free: `lu_jit_pic_note` runs this specializer (`runtime/jit/jit.c:2236-2246`).

### Generic LOAD_ATTR (`ceval.c:3287-3386`): one pass
`lu_getattr_native` becomes the single pass and is called once. It gains an out-parameter for "data descriptor whose `__get__` is a Python function", so the interpreter can still push that frame. Switch on the read word:

- **Exact module:** dict probe, else the kernel.
- **TYPE:** the class half of `attr_shortcut`, without its predicate.
- **GENERIC and GENERIC_GETATTR:**
  1. One MRO lookup of the name.
  2. `__class__` answered when the descriptor found is object's.
  3. Native data descriptor.
  4. Instance storage.
  5. Bind or return the class value.
  6. On a miss, kernel entry `generic_getattr` for GENERIC (static builtins skip the hook), `getattr` for GENERIC_GETATTR.
- **CUSTOM:** straight to the kernel, no lookups.
- **SLOT:** `PyObject_GetAttr`.

Goes away: `object_attr_hook` (753-760) and its five uses, the predicates in `attr_shortcut` (772-776), and lines 3309-3368 collapsing to about 35.

### Generic STORE_ATTR and DELETE_ATTR (`ceval.c:3433-3482`)
`lu_setattr_native` switches on the store or delete field, by whether the value is NULL (fixes D2):
- TYPE: `type_setattr_native` without its lookup (`descr.c:1363`).
- GENERIC: as now, plus a native delete through `lu_attr_store` with a NULL value (`runtime/lucifer.h:346-348`), leaving only a missing name to the kernel.
- Kernel entry: `generic_setattr` for GENERIC, `setattr` or `delattr` otherwise.
- SLOT: `PyObject_SetAttr`.

Goes away: `descr.c:1376-1382` and the slot tests at `ceval.c:3444-3445, 3463`.

### Kernel hooks (`object.py`)
One new pure primitive, `attr_way(tp, which)`, returns the field. For a thunk type that is UNSET it classifies, so the kernel never sees UNSET.

`getattr_hook` becomes:
- GENERIC → `generic_getattr`
- TYPE → `type_getattr`
- MODULE → `module_getattr`
- GENERIC_GETATTR → suppressed generic lookup, then `call_special` on the `__getattr__` found
- otherwise (CUSTOM and SLOT) → today's tail: look up `__getattribute__`, call it, catch AttributeError only when a `__getattr__` exists

The rest:
- `lookup_attr` keeps three suppressed branches and delegates the rest.
- `setattr_hook` and `delattr_hook` are one test each.
- `builtins.py:114` uses the word for its `__getattribute__` half.
- `object.py:112-113` uses the two write fields (fixes D6).

Goes away:
- the ladder in both functions (126-136, 139, 161-170)
- every `generic_wrapper` call, and the primitive (`descr.c:1621-1636` and its table entry at 1910)
- the four module-level slots at `object.py:152` that only fed identity tests

Stays in Python: the generic algorithms, `_type_getattr`, `_module_getattr`, the try/except fallback, `call_special` of custom hooks, and error text.

In compiled code `attr_way` folds to a constant under the type's version, with a fold in `runtime/jit/eval_prims.c` beside `type_lookup`'s at line 345. With an unknown type it is one pure call in place of two `type_lookup` calls and up to two `generic_wrapper` calls.

### JIT
| Site | Becomes |
|---|---|
| `eval_attr.c:160-162` | read GENERIC and store GENERIC |
| `eval_attr.c:588-589` | metaclass read TYPE |
| `eval_call.c:955-966` | read GENERIC or GENERIC_GETATTR, plus the `__class__` lookup |
| `jit.c:2253-2254` | store GENERIC |

Each is read between `lu_ev_class_as_of` and `lu_ev_class_relied`. The unit's dependency stays the type's version.

### C-API
- `PyObject_GetAttr` and `PyObject_SetAttr` keep calling the slot.
- The thunks (`slots.c:133-147`) run the native pass first, then the kernel entry the word names. C callers on builtin instances stop entering Python for found attributes.
- `PyObject_GenericGetAttr` and `PyObject_GenericSetAttr` call the GENERIC case body directly, with no word test: the caller asked for that algorithm. This fixes D4.
- `_PyObject_GenericGetAttrWithDict` (`runtime/capi/internal.c:216-240`) has no predicate and is unchanged.

## 5. Semantics that must not change (tests against the oracle)

One new program, `tests/kernel/attr_protocol.py`. Every case runs in a loop long enough to specialize and compile, then again after the mutation.

1. **Lookup order.** Data descriptor on the type, instance dict, non-data descriptor, class attribute; `__getattr__` only on AttributeError, including one raised by a property getter or an unset slot.
2. **Python `__getattribute__` calling `object.__getattribute__`**, with and without `__getattr__`.
3. **Hooks on a base with live subclasses.** `__getattr__`, `__getattribute__`, `__setattr__`, `__delattr__` added to and removed from a base while subclasses and their instances exist; also through `__bases__` assignment and a custom `mro()`.
4. **Classes over C generic slots** (ast node classes): same as 3, plus the "no `__dict__` for setting new attributes" message (D6).
5. **C type with its own getattro, subclassed in Python**, with and without overrides, hooks gained and lost mid-loop (`tests/capi/ready_run.py`, the Vec cases).
6. **Old-style `tp_getattr`-only types** and their Python subclass (`OldAttr`, `OldAttrSub`, `OldAttrPy` in `ready_run`).
7. **Metaclasses.** `__getattribute__`, `__getattr__` and `__setattr__` on a metaclass; class attribute loads and stores before and after.
8. **Metaclass change.** `Cls.__class__ = Meta` under a hot class-attribute load (question 5).
9. **Modules.** Module `__getattr__` added and removed; a module subclass with and without its own hooks; `__class__` assignment between module subclasses.
10. **Copied wrappers.** A wrapper copied into an unrelated class raises TypeError; with `__getattr__` present the generic wrapper is accepted. type's and module's `__getattribute__` copied into other classes.
11. **`super()` bypasses `__getattribute__`.**
12. **Descriptors.** `__slots__` members, C members and getsets, tuplegetters, on all of the above.
13. **Independent write overrides.** `__setattr__` and `__delattr__` each overridden alone; in particular only `__delattr__` overridden with a `del` of a slot member (D2).
14. **Descriptor with `__set__` and no `__delete__`**, and the reverse.
15. **isinstance negatives** on classes with each protocol.
16. **Threads** (handoff lane): another thread flipping a base's hooks under specialized and compiled loads and stores (extends `threads_ast_attr_hooks`); two threads assigning hooks on one class, then checking the final behaviour (D7).

## 6. Stages

Line counts are estimates. Every stage runs `tools/floor quick` (the per-change gate in AGENTS.md); the gate column adds what to select on top.

| Stage | Change | Files | Lines (+/−) | Gate | Expected to move |
|---|---|---|---|---|---|
| 1 | The word and the decision function; all refresh call sites; the flag mask; the atomic or at `attrs.c:209`; the specializer switched (bracketed). Every other C site keeps its predicate and adds one check line. `prim_generic_wrapper` checks "true implies the word says generic", so the Python hooks do not grow. | `lucifer.h`, `slots.c`, `prim/type.c`, `frozen.c`, `typespec.c`, `attrs.c`, `capi/object.c`, `specialize.c`; one line each in `ceval.c`, `descr.c`, `eval_attr.c`, `eval_call.c`, `jit.c` | +260 / −50 C; +250 tests | Kernel lanes jit-off, plain, jit, jit-deopt; handoff; tools/jitcheck; check-capi-ext | pyflakes JIT off: most of the 0.50M generic LOAD_ATTR at about 490 instructions (1.7%) and of the STORE_ATTR row (0.37M at about 280, 0.7%) become specialized forms. JIT on: the same sites gain cache and pic shapes; unmeasured. |
| 2 | Interpreter generic paths in one pass; the native functions keyed on the word; native delete; D2 fixed | `ceval.c`, `descr.c` | +80 / −110 | Kernel lanes; handoff; jitcheck (the helpers share the native functions) | Remaining generic LOAD_ATTR cost per execution; DELETE_ATTR (0.21M at about 493) to roughly a native store's cost; `type_lookup` share under the load and store helper rows (17%, 29% and 24% of 2.83%, 1.03% and 0.80% of cycles in luc-mopl). |
| 3 | Kernel hooks read the word through `attr_way`; fold in `eval_prims.c`; delete `generic_wrapper` | `object.py`, `builtins.py`, `prim/type.c`, `descr.c`, `eval_prims.c` | +60 / −70 | All four kernel lanes; handoff; jitcheck; check-capi-ext; tomli_loads and the C1 getattr probe compared for instructions | Inlined size of the hooks (IR instructions of a getattr probe unit; compile instructions on pyflakes, where the generic attribute path is copied into 120-165 units per `.tickets/luc-1kz0.md:37`); the getattr probe's instructions per iteration. |
| 4 | JIT sites read the word; checks deleted | `eval_attr.c`, `eval_call.c`, `jit.c` | +15 / −30 | jit and jit-deopt lanes; jitcheck | Nothing large; removes the last lookup copies. |
| 5 | Thunks run the native pass; generic C-API functions call the generic body (D4) | `slots.c`, `capi/object.c`, `ceval.c` | +40 / −20 | check-capi-ext; a numpy file from the floor slice; kernel lanes | C callers of `PyObject_GetAttr` on builtin instances. |

### The stage 1 check
Asserts are compiled into every build (there is no NDEBUG), and a hook assignment has a window where MRO and word legitimately differ. So the check must not fire on that window.

- Form: `LU_ATTR_CHECK(site, tp, expected, legacy)`.
- On a mismatch it calls out of line, takes `attr_lock`, and recomputes the classification:
  - If the stored word is not current, a writer is mid-sequence: return.
  - Otherwise re-evaluate once, and call `Py_FatalError` naming the site and the type.
- It passes when the site's predicate implies the word's answer, and when the word's answer implies the predicate or the one named gap. The gap is "the type finds a C wrapper of the generic function" for lookup-only sites, or "the slot is the thunk" for slot-only sites.
- Each later stage deletes the checks of the sites it switches.

## 7. Risks and open questions

1. Does any extension in the floor's corpora write `tp_flags` of a ready type non-atomically, or keep something in `tp_cache`? The first loses a word update; the second rules out the fallback location.
2. Does the boot-end pass over static types cost a measurable share of `-c pass`? If so, can it be limited to types whose MRO is not just object?
3. After stage 2 the generic paths trust the word and no longer compare the slot. Is an extension that overwrites `tp_getattro` after `PyType_Ready` without `PyType_Modified` something we must tolerate? If yes, the SLOT and GENERIC cases keep one slot compare.
4. A metaclass with `__getattr__` and type's `__getattribute__` loses the class forms it gets today. Does any census program have one hot? If so, add a read value for type-with-fallback.
5. Does `Cls.__class__ = Meta` invalidate a LOAD_ATTR_CLASS entry? `set_class` invalidates only the old metaclass's version (`prim/type.c:2574`), and the entry is keyed by the class's own tag (`ceval.c:3144`). What does CPython do?
6. Exact modules read `md_dict` before type-level data descriptors (`ceval.c:800, 3298`). Is a module dict holding a key such as `__class__` or `__dict__` handled as CPython does? Unchanged by this design, and unverified.
7. A subclass is registered with its bases before its own `update_slots` runs (`prim/type.c:1836-1840`). Can a base's `_remro_slots` reach a subclass whose MRO is not yet set, and is `prim_update_slots` safe on it?
8. What exactly is the census row named STORE_ATTR_FAST at 280 instructions: specialized stores that fall through to the generic path, or the generic store itself? The stage 1 estimate for stores depends on it.
9. Are D2, D4, D6 and D7 real? Each needs its failing test (cases 13, 5, 4 and 16) before the stage that fixes it.
10. Does the smaller `getattr_hook` change which callers inline it enough to move some unit past its budget in the other direction? Stage 3 compares tomli_loads for that reason.

### Files
runtime/types/slots.c, runtime/interp/specialize.c, runtime/interp/ceval.c, lib/kernel/object.py, runtime/prim/type.c.
