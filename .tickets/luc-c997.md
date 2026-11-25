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


## State (2026-11-25, later)

Branch luc-attrword, rebased on origin/master (7260c37d), not pushed. Stages 1 and 2 are built and gated together (the coordinator's ruling: stage 1 lands only with stage 2), with the adversarial review's fixes 1-4 and 6 folded in after stage 1; 5 is recorded below. Next: stage C of the brief, a specialized form for instance dicts at `tp_dictoffset` (measured below at about 2% of pyflakes JIT off, above its 0.5% bar), not started; then stages 3-5.

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

## Notes

### Open questions for stage 1, answered from the code (2026-11-25)

1. No foreign writer. Every write of `tp_flags` in the runtime outside type creation or `PyType_Ready` (before the type is visible) is atomic, except `runtime/object/attrs.c:209` (made atomic here) and `PyType_Ready`'s `Py_TPFLAGS_READY` (made atomic here too, since the word is stored just before it). The extensions in the tree (`tests/capi/*.c`) and the vendored numpy and CPython `Modules/` set `tp_flags` only in static initializers or on a type they are creating (`_testcapimodule.c:3169`, before ready). Nothing reads or writes `tp_cache`. The word stays in `tp_flags`.
2. The pass costs 0.72M instructions on `-c pass` (116.20M with one pass, 116.93M with the pass run twice, JIT off; 133.29M and 134.01M JIT on): 0.6%. Not worth limiting yet.
7. Safe. The C half of class creation registers the class with its bases (`add_subclass`) before `type.py` sets its MRO and runs `update_slots`. A base's `_remro_slots` that reaches it then decides its slots with `tp_mro` NULL; `lookup_in_mro` walks the `tp_base` chain in that case (`prim/type.c`, `lookup_in_mro`), so the lookups are sound, and `prim_update_slots` reads nothing else. The class's own `update_slots` runs after its MRO is set and decides again under `attr_lock`, so the last decision reads the full MRO. Until its first decision the class's word is UNSET (heap types copy only named bits from the base), which the accessor classifies on the fly.
8. `STORE_ATTR_FAST` is the interpreter label shared by the three specialized stores (`STORE_ATTR_SLOT`, `STORE_ATTR_WITH_HINT`, `STORE_ATTR_INSTANCE_VALUE`, `ceval.c` dispatch table), counted inclusive of their C callees (`lu_ns_store`, `lu_inline_store`); a failed guard jumps to `op_STORE_ATTR`, charged elsewhere. So it is not the generic store. The generic STORE_ATTR shares its label with DELETE_ATTR (`op_STORE_ATTR:` / `op_DELETE_ATTR:` stacked, the census charging stacked labels to the last), so the census row "DELETE_ATTR 0.21M x 493" holds the generic stores' instructions with only DELETE_ATTR's executions. Stage 1 moves generic stores out of that row, not out of STORE_ATTR_FAST; the counts below say how many.

### 2026-11-25 stage 1 as built (not landed)

- The word (`runtime/lucifer.h`): three 3-bit fields at bits 41, 44, 47 of `tp_flags`; `lu_attr_way` loads it relaxed and classifies an UNSET field on the fly. `type.__flags__` and `PyType_GetFlags` mask bits 40 and up (D8).
- One decision (`runtime/types/slots.c`): `thunk_way` is what the kernel hook does under the thunk; `lu_attr_classify` maps a slot to its field; `attribute_thunk` decides a class's slot from `thunk_way`. `lu_set_slot` decides, writes and stores the word for an attribute slot under `attr_lock` (CAS, release). `keeps_object_hook` is gone. One change in what is decided: a class with object's `__setattr__` and a generic C wrapper as `__delattr__` (or the reverse) now gets `PyObject_GenericSetAttr` instead of the thunk; both run generic access.
- Refreshes: `lu_set_slot` (class creation, dunder assignment and deletion, `__bases__`, custom `mro()`); `ready_type` (before its version; `Py_TPFLAGS_READY` is now an atomic or); `PyType_FromSpec`; `PyType_Modified` (the type and its subclasses, from their slots, before the versions are cleared); the boot-end pass (`lu_attr_boot`, from `frozen.c` between `kernel_booted` and `specializing`): every static type, and every type under object, deciding again the attribute slots of heap types whose slots are thunks or generic (classes the kernel made while its bases' dicts were still filling). Words are stored only once the kernel has booted. `attrs.c`'s or of `Py_TPFLAGS_INLINE_VALUES` is atomic.
- Lock order: `attr_lock` before `type_lock` (lookups assign versions under `type_lock`); the walks over subclasses take snapshots and hold no lock while deciding. The first build walked under `type_lock` and deadlocked (capi_conv_run and jit_import_hot hung).
- `attr_lock` serializes every attribute slot decision with its write, so D7 is closed for these slots as a side effect of the design's lock; its test (case 16, second half) is not written, as asked.
- The specializer reads the word inside `analyze`, after the version (D3): instance forms need read GENERIC or GENERIC_GETATTR (LOAD_ATTR_PROPERTY GENERIC), class forms the metaclass's read TYPE, stores store GENERIC. `default_getattribute`, the slot gates and the `__getattr__` and `__setattr__` lookups are gone; exact modules are as before.
- Checks (`LU_ATTR_CHECK`): the macro's value is the site's predicate, evaluated once; on a disagreement with the word, unless the kernel is booting or the type has one of the site's known gaps (bits: a generic C wrapper; a thunk slot; a `__getattr__`), the predicate, the word, the gap and then whether the word is current are evaluated again under `attr_lock`, and the check is fatal only if the disagreement is still there and the word and slots are what a refresh would make them now (a writer between its dict change and its slot rewrite shows as not current). Sites: `object_attr_hook`, `attr_shortcut` (gap: metaclass `__getattr__`), LOAD_ATTR's and STORE_ATTR's slot gates, LOAD_ATTR's getter frame (`__getattr__`), `lu_setattr_native` (both branches), `type_setattr_native`, `instance_kind`, `class_attribute`, `known_subclass`, the pic store, `generic_wrapper`. Not checked: `PyModule_CheckExact` (a type identity; module subclasses share MODULE) and the kernel-entry choices that reuse the checked slot test. Two firings, both in threads_attr_protocol (handoff, a thread flipping hooks on a base and a metaclass) and both the check's own races, not disagreements of a site: `object_attr_hook` on Sub (word 6/3/1, now 1; legacy 1) when the check re-read a word hoisted before the macro, so a writer that finished between the predicate and the check looked like a disagreement (fixed: everything is evaluated again under the lock); `attr_shortcut` on Meta (word 3/2/2, legacy 1) when the gap (the metaclass's `__getattr__`) was tested before the lock and the flipper added `__getattr__` in between (fixed: the gap is tested again under the lock). After that, three handoff runs of the thread programs and every other program: no firing.
- Tests: `tests/kernel/attr_protocol.py` (cases 1-4, 7, 9, 10, 12, 15; 5 and 6 are `tests/capi/ready_run.py`'s), `threads_attr_protocol.py` (16, first half), jitcheck lane `genericslots` (`jit_generic_slot_attrs.py`, stores and loads on two Python subclasses of decimal.Decimal): fails on master (55349 helper calls at 20000 iterations, 95349 at 40000: two per iteration), passes now (16500, 16500). Its first version used ast nodes and failed both before and after (359920, 719920), which is how the dict layout showed up. A class attribute read through such an instance (LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES) still costs compiled code one helper per iteration, before and after; not this stage's. The new programs pass on master too.
- Cost (instructions, pinned, two runs per arm; master 983e7912 against stage 1; harness sizes: AWFY Richards 5 100, DeltaBlue 5 12000, tomli_loads `-l 2`). With no generic execution moved, the stage pays for its checks and word reads.

| row | JIT off, master | JIT off, stage 1 | JIT on, master | JIT on, stage 1 |
|---|---|---|---|---|
| -c pass | 115.1M | 116.2M (+0.95%; 0.72M is the boot pass) | 132.2M | 133.3M (+0.8%) |
| stdlib imports | 1154M | 1153M | 1356M | 1356M |
| pyflakes | 12.59G | 12.62G (+0.24%) | 9.95-9.99G | 10.08-10.09G (+1.0-1.3%) |
| tomli_loads | 616.0G | 612.5G (-0.6%) | 103.5G | 103.5G |
| Richards | 405.4G | 403.1G (-0.6%) | 60.35G | 60.71G (+0.6%) |
| DeltaBlue | 73.78-73.84G | 73.85G | 25.45G | 25.63G (+0.7%) |

  pyflakes and the AWFY rows were measured once before the last change, which keeps `generic_wrapper`'s check off the kernel hooks' path (pyflakes then +0.9% JIT off, +2.2% JIT on); the AWFY, tomli, pass and imports rows are from that build.
- Gate: attr_protocol and 60 attribute, descriptor, type, class, super, property, module and C-API neighbours in jit-off, plain, jit, jit-deopt; 13 thread programs in handoff (three times); all 16 check-capi-ext programs; tools/jitcheck: pass, except that capi-ext and the full jitcheck ran on the build before the last two check fixes (the gap tested under the lock, `generic_wrapper`'s fast path), which touch only the check's slow path and that one primitive; the new lane, the four kernel programs and the thread programs ran on the final build.

### 2026-11-25 review fixes, stage 2, numbers

- Stage 1's JIT-on cost, by profile (pyflakes JIT on, instructions sampled, master against stage 1): no check disagreed on a hot path and no word was UNSET after boot (a counting build: 2.5M check evaluations, 32 disagreements, all `pic store`); the cost was `object_attr_hook` no longer inlined (the macro's slow path in its body; 36M on pyflakes, 76M on DeltaBlue) and `prim_generic_wrapper` finding its field with two `PyUnicode_EqualToUTF8` calls, each saving and restoring the error state (about 50M). The field now comes from the wrapper's slot name, and the check's slow path is cold; stage 2 deletes `object_attr_hook`.
- Review fixes: `lu_set_slot` records which attribute slots it decided (`LU_TPFLAGS_GET_DECIDED`, `_SET_DECIDED`), and `attr_current` compares those, so a class over a C type's own getattro is checked mid-write too (defect 1; `tests/capi/ready_run.py` "vec flip" aborted on stage 1); a check aborts only if the type's tag, taken under the lock, is non-zero and unchanged at the end, and an UNSET word is never current (defect 2; `threads_attr_protocol.py` "contested" aborted on stage 1 in 5 of 5 runs); the specializer compares the slot inside the bracket (3); `Py_TPFLAGS_READY` after `add_subclass` is an atomic or (4); the subclass walk visits each class once, with a C pointer set (a Python set at boot end lost the boot); the kernel hook's check finds its field without calls (6). Not done (5): `attr_lock` and `type_lock` are PyMutexes, which the fork path's lock registry does not cover; a child forked while another thread holds either would deadlock on it. New tests: attr_protocol cases 8, 11, 13, 14 (and case 15's property liar, which assigned to `Liar.__class__` and so never ran); the Decimal subclass's stores under a later `__setattr__` and `__delattr__` (passes on master and stage 1 as well).
- D2 is real: on master `del o.s` of a `__slots__` member skips a `__delattr__` defined alone (attr_protocol "one hook DelOnly": master gives 200 calls, CPython 400).
- Stage 2: `lu_getattr_native` is the single pass (exact module dict; TYPE: the class half; GENERIC and GENERIC_GETATTR: `__class__`, native descriptor, instance dict, class value; else NULL), with the getter of a Python data descriptor handed back in `kargs` for the interpreter's frame; `lu_setattr_native` switches on the store or delete field (TYPE, GENERIC with native delete); kernel entries skip the hook where the stored word is GENERIC. Departures from the design: (a) the native read and write cover a dict at `tp_dictoffset` as well as a managed one (CPython's algorithm does; it is where most of pyflakes' generic accesses were); (b) classes are never owners of the GENERIC paths (a metaclass with object's hooks goes to the kernel); (c) exact modules are probed in LOAD_ATTR whatever their type's slot, since the kernel's boot sets module's slot after the first modules; (d) the kernel entry follows the stored word only, not one classified during boot; (e) the slot compare stays on the generic cases (question 3). The getter out-parameter was first a local array, which moved eval_frame's layout: tomli_loads +2.6% and Richards +2.1% JIT off with identical kernel calls and generic counts; reusing `kargs` removed it.
- Generic executions, pyflakes JIT off, master and head (counting build): LOAD_ATTR 615,054, STORE_ATTR 222,649, DELETE_ATTR 31,836, the same counts (479,927 and 176,864 of them on owners with a dict at `tp_dictoffset`); instructions 12.56G and 8.85G.
- Gate (head): attr_protocol and 60 neighbours in jit-off, plain, jit, jit-deopt; 13 thread programs in handoff, three runs; check-capi-ext, all 16; full tools/jitcheck; all pass, no check fired. After the `kargs` change, the kernel lanes, one handoff run and the generic-slot lane again.

| row | JIT off, master | JIT off, head | JIT on, master | JIT on, head |
|---|---|---|---|---|
| -c pass | 115.2M | 113.8M (-1.2%) | 132.3M | 130.2M (-1.6%) |
| stdlib imports | 1140M | 1131M (-0.8%) | 1348M | 1334M (-1.0%) |
| pyflakes | 12.60G | 8.82G (-30.0%) | 9.90G | 8.33G (-15.9%) |
| tomli_loads | 616.0G | 612.8G (-0.5%) | 103.4G | 103.3G (-0.1%) |
| Richards | 405.4G | 400.4G (-1.2%) | 55.52G | 55.52G (0.0%) |
| DeltaBlue | 73.73G | 70.46G (-4.4%) | 24.94G | 25.01G (+0.3%, mins; means +0.1%) |

  Instructions, pinned, two runs per arm, minimum shown; master 7260c37d.
- Stage C's measure: 4 loads and 1 store per iteration, JIT off, 10^6 iterations: an ast node (dict at `tp_dictoffset`) costs 14,411 instructions per iteration on master and 2,374 on head; a plain instance (specialized forms) 1,060 and 1,049. A specialized form would save about 265 instructions per access, times 656,791 such accesses: about 174M, 2% of pyflakes JIT off. Above the bar; not built yet.

### 2026-11-25 stage 2 review fixes (adversarial review: land after fixes)

Each reproducer run against the oracle on master (7260c37d) and on stage 2 before the fix, JIT off and on:

- H1 (fails before, both): a member or getset copied into a class it does not describe was read and written at its offsets in the other object (no `descr_check`): `b.a = 1` succeeded and read back, a C getset (`ready.Vec.sum`) answered a float from a foreign object, a copied `function.__name__` getset crashed (exit 139). Fixed in `lu_descr_get_native` and `lu_descr_set_native` (the kernel raises), and in the forms built on the same fact: the specializer's LOAD_ATTR_SLOT and STORE_ATTR_SLOT (they specialized the foreign member on the second execution and wrote at its offset), LOAD_ATTR_PROPERTY for a kernel getset, and the tier's virtual-instance loads and stores of slot members. Tests: `attr_native_guards` (members, builtin getsets, get, set, delete), `ready_run` "stranger" (a C getset and a C member, a static type's getset and member).
- M1 (fails before, both; older than this branch): an instance dict key that is a str subclass. Reads, method loads, stores and deletes missed it (a duplicate key on store, the class's method on load) in the native paths, the managed-dict hint store and the method-without-shadow guard (interpreter and compiled), and in the kernel's own `instance_load`. `lu_attr_lookup` hands "Python must compare" to the kernel; `instance_load` asks the dict with Python; `lu_ns_exchange` probes once under the dict's lock and, when the table holds a key that is not an exact str, stores or deletes with `PyDict_SetItem` or `PyDict_Pop`; every instance store (native, kernel, hint form in both tiers) goes through it. Test: `attr_native_guards` (managed dict, an `ast.AST` subclass and `ast.Name`, a key whose `__eq__` answers).
- M2 (does not fail before: no interleaving found that frees the dict in a run): `lu_ns_exchange` holds the dict across the store and hands the replaced value back, released by the caller after the dict's lock; the generic store (`generic_setattr_native` now ends in `lu_attr_store`) and both hint stores use it. Tests: `threads_dictoffset_attrs` (first stores racing, `__dict__` replaced under reads, stores and deletes, a finalizer replacing the dict its store went into), `ready_run` (the same on a static C type).
- L1: one slot compare, `lu_attr_slot_ours`, used by `lu_getattr_native` before its switch (the TYPE arm had none), the isinstance negative, `lu_setattr_native` and the specializer.
- L4: `lu_getattr_native` no longer returns NULL with an error pending: a failed bind (no memory) is cleared and the kernel asks again and raises, as `lu_attr_lookup` already does for a box. A test in LOAD_ATTR itself moved `eval_frame`'s layout (tomli_loads +2.6%, Richards +2.1% JIT off, as stage 2's first getter array did), and so did a local for the replaced value in the hint store: the store calls `lu_ns_assign`, and the method guard `lu_ns_lacks`, so the loop's body is as before. DeltaBlue JIT on was +0.7% against master (+0.5% of it stage 2's, measured again today: 25.08G against 24.95G; the stage 2 note's 25.01G did not reproduce), in steady state, not warmup (5 and 15 iterations): the exchange took the old value with a reference of its own, `lu_ns_lacks` probed twice, and stage 2's single pass asked `lu_descr_get_native` about every descriptor. The exchange now swaps the value out of its entry (`swap_value`, shared with `lu_dict_replace`), `lu_ns_lacks` probes once, and only data descriptors are asked: DeltaBlue JIT on -0.4% against master.
- L5: `PyType_FromSpec` clears the decided bits and rewrites `tp_flags` with a CAS.
- L7: cannot happen. `ready_type` refuses a heap base (`prim/type.c`, "is not dynamically allocated but its base type ... is"), and only heap types get `Py_TPFLAGS_MANAGED_DICT` with `tp_dictoffset == -1` (`type.__new__`, and spec types made through it, which copy the flag from the base); no static type sets the flag. A static type's inherited negative offset is CPython's own convention, which `lu_dict_ptr` computes.
- L3 not fixed: passing a C getter's error through (instead of clearing it for the kernel to call the getter again) is not two lines: `builtin getattr` with a default and `hasattr` reach `lu_getattr_native` from `lu_native_call` and would then raise, and GENERIC_GETATTR must still reach `__getattr__`. For stage 5, with L2/D4 and L8/D6.
- D4 is real: `capi_generic_getattr` (ctypes) shows `PyObject_GenericGetAttr` on a class answering type's lookup through the bases (`B.x`, `B.m` from a base, where CPython raises) and on a module reading the dict before `__class__`; listed in kernelcheck's XFAIL as luc-c997 until stage 5.
- Also added: `attr_protocol` "isinstance property gained/lost" (the `__class__` property reached through `__bases__` assignment mid-loop; passes on master); `ready.Dicted` (a static type with its dict at `tp_dictoffset`, members of each kind, a getset, a Python subclass: reads, stores, deletes, `__dict__` replaced, emptied, shared).
- Gate (head 0b0381fc's runtime): attr_protocol, the new programs and 60 neighbours in jit-off, plain, jit, jit-deopt; 14 thread programs in handoff, three runs; check-capi-ext, all 17; full tools/jitcheck: all pass (capi_generic_getattr XFAIL). The new kernel programs pass in a fresh worktree built with `make -j4` alone (plain, jit; threads in handoff).

| row | JIT off, master | JIT off, head | JIT on, master | JIT on, head |
|---|---|---|---|---|
| -c pass | 115.1M | 113.8M (-1.2%) | 132.2M | 130.2M (-1.6%) |
| stdlib imports | 1154.4M | 1138.0M (-1.4%) | 1356.1M | 1342.3M (-1.0%) |
| pyflakes | 12.59G | 8.84G (-29.8%) | 9.93G | 8.35G (-15.9%) |
| tomli_loads | 616.07G | 612.95G (-0.5%) | 103.41G | 103.42G (+0.0%) |
| Richards | 405.39G | 399.98G (-1.3%) | 55.52G | 55.52G (0.0%) |
| DeltaBlue | 73.71G | 70.40G (-4.5%) | 24.96G | 24.84G (-0.5%) |

  Instructions, pinned, two runs per arm, minimum; master's runtime is 7260c37d's (origin/master since added tests and tools only).

### 2026-11-25 focused review of the fix commits

- M-1: on a table with keys that are not exact strs the exchange stored through `PyDict_SetItem`, which released the replaced value inside, and handed back no old value: a class's old value would die before `lu_type_modified` took back its cache tags. Not reachable today (Lucifer refuses non-str keys in a class dict: `type('A', (), {0: 0})` raises SystemError and `setattr(cls, S('y'), v)` TypeError, where CPython makes both; divergences of their own, not fixed here), so no failing test; fixed anyway: such a table is probed natively under the lock (`probe_native`) and the value swapped out; only a key Python must compare goes through `PyDict_GetItemRef` and `PyDict_SetItem` with the old value held across. Test: `attr_native_guards` "general table" (an int key and a str-subclass key, the replaced value's finalizer; passes before and after).
- L-1: `lu_ns_lacks` answers "lacks" only from an unchanged unicode probe; a table that changed under it deopts.
- L-2: `PyType_FromSpec` clears the decided bits under `attr_lock` (`lu_attr_undecide`) before it writes any slot, so no walk can decide a spec type's attribute slots after that.
- L-3: luc-deth.
- Brief gate: the kernel programs (64) in four lanes, threads in handoff, ready_run. DeltaBlue JIT on 24.75G, Richards off 399.98G, pyflakes off 8.87G, tomli off 613.0G: no row moved against the numbers above.
