No changes to `Lib/`.

Tools/peg_generator/pegen/ is copied from the same tag to generate
lib/compiler/parser.py. Modifications:

- Tools/peg_generator/pegen/python_generator.py: the generated Python parser
  follows the C generator's semantics, so that it parses exactly what the C
  parser does: items fail only when their call returns None (optional items
  always succeed, lookaheads are booleans); only rules marked `(memo)` and
  left-recursive leaders are memoized; alternatives that consist of a single
  `invalid_` rule run only in the second (error) pass, and `*_without_invalid`
  rules switch that pass off; keywords, soft keywords and exact tokens are
  matched by token type as in the C parser; `EXTRA` in an action expands to
  the node location the C parser computes (start of the rule's first token,
  end of the last non-whitespace token); default actions with several items
  return the parser's dummy name; generated methods take the parser as `p`.

Every future modification to this copy of CPython must be listed here, with a
brief summary as required by clause 3 of the PSF License. No files from Lib/
have been excluded.

## include/

`include/` (top level of this repository) is `Include/` from the same tag,
copied verbatim except that `Include/internal/` is excluded. `include/pyconfig.h`
is not part of `Include/`: it is the file `configure --disable-gil` generated
for the cp314t oracle build of this tag (`Py_GIL_DISABLED 1`). It contains no
paths. `include/LICENSE` is a copy of this directory's `LICENSE`.

## runtime/

C under `runtime/` adapted from this tag's sources, under the PSF License
(`LICENSE` in this directory):

- `runtime/interp/marshal.c` from `Python/marshal.c` (`r_object` and its
  helpers): reads from memory only; the reference list is a C array instead of
  a list; ints are built from digits directly instead of through
  `PyLongWriter`; version-0 floats parse with `strtod` instead of `dtoa.c`;
  dict items require exact-str keys until the kernel's dict exists; no audit
  event.
- `runtime/object/hash.c` from `Python/pyhash.c` (SipHash-1-3,
  `Py_HashBuffer`, `Py_HashPointer`, `_Py_HashDouble`), `Python/bootstrap_hash.c`
  (`lcg_urandom`, the seed policy over `getrandom`), `Objects/longobject.c`
  (`long_hash`) and the xxHash lanes of `Objects/tupleobject.c` and
  `Objects/sliceobject.c`: one function serves tuples and slices; no other
  algorithms are built.
- `runtime/object/set.c` from `Objects/setobject.c` (probing, resize policy,
  `frozenset_hash`): growth only, keys compared by layout equality until the
  kernel's rich comparison exists.
- `runtime/object/dict.h` and `runtime/object/dict.c` from
  `Include/internal/pycore_dict.h` and `Objects/dictobject.c` (the keys-table
  layout, unicode-keys lookup, insertion and resize): exact-str keys only.
  Deletion (`delitem_common`'s dummy index) and in-order iteration over the
  entries are added for the namespace primitives.
- `runtime/interp/call.c` from `Python/ceval.c` (`initialize_locals`,
  `missing_arguments`, `too_many_positional`, `positional_only_passed_as_keyword`,
  `_PyEval_FormatKwargsError`, `_Py_Check_ArgsIterable`), `Python/suggestions.c`
  (`levenshtein_distance`, `_Py_CalculateSuggestions`) and `Objects/object.c`
  (`_PyObject_FunctionStr`): messages are formatted with printf into a
  512-byte buffer and long lists of names are truncated; `*` operands are
  tuples and lists and `**` operands exact dicts until iteration and the
  mapping protocol exist.

- `runtime/thread/parking_lot.c` from `Python/parking_lot.c`: each waiter
  sleeps on a Lucifer wait record (`lu_park`) instead of a `_PySemaphore`,
  and is woken by its wait id (`lu_unpark`); bucket locks are registered
  `pthread_mutex_t`s instead of `_PyRawMutex`; the waiter list is a
  NULL-terminated doubly linked list instead of `llist`. A waiter that times
  out after an unparker dequeued it returns `Py_PARK_OK` without waiting for
  the wake (the wake's id is stale by then), so `_PyParkingLot_UnparkAll`
  wakes under the bucket lock. `_PyParkingLot_AfterFork` is not ported yet.
- `runtime/thread/lock.c` from `Python/lock.c`: `PyMutex` only
  (`_PyMutex_LockTimed`, `mutex_unpark`, `_PyMutex_TryUnlock`,
  `PyMutex_Lock`/`Unlock`/`IsLocked`); the clock is `CLOCK_MONOTONIC` read
  directly. The other lock kinds of that file are not ported yet.
- `runtime/thread/critical_section.c` from `Python/critical_section.c` and the
  inline functions of `Include/internal/pycore_critical_section.h`: the
  exported functions carry the inline bodies; the world-stopped shortcut of
  the slow paths is omitted until stop-the-world exists.
- `runtime/thread/lock.h` from `Include/internal/pycore_lock.h`,
  `pycore_parking_lot.h` and `pycore_critical_section.h`: the declarations
  the three files above share.

- `runtime/prim/type.c` from `Objects/typeobject.c` (`_PyType_Lookup`,
  `PyType_IsSubtype`, the static builtin type index in `tp_subclasses`,
  `best_base`, `solid_base`, `inherit_slots`, `type_new_slots`,
  `type_new_copy_slots`, `type_new_alloc`, `type_new_set_attrs`,
  `type_new_descriptors`, `compatible_for_assignment`,
  `object_set_class`, `subtype_dealloc`, `subtype_dict`, the `__new__`
  safety check of `tp_new_wrapper`) and `Python/structmember.c`
  (`PyMember_GetOne`, `PyMember_SetOne`): types are made ready from a
  static table instead of `PyType_Ready`; the MRO, slot dispatch and the
  attribute semantics are kernel Python; subclass lists are lists of strong
  references until weakrefs exist; instance attributes live in the managed
  dict only (no inline values); `__slots__` are sorted by code point and
  identifiers are checked for ASCII only; members support the object and
  integer kinds.
- `runtime/object/descr.c` from `Objects/descrobject.c` (descriptor and
  `method-wrapper` layouts, members and getsets, `descr_check`'s and
  `method_check_args`'s messages), `Objects/classobject.c` (`PyMethod_New`,
  `method_vectorcall`) and `Objects/funcobject.c` (the classmethod and
  staticmethod layouts): `__get__` and `__set__` are kernel Python; kernel
  functions are descriptors and builtins over an `lu_kernel_def`.
- `runtime/types/slots.c` from `Objects/typeobject.c` (`slot_tp_*`,
  `slot_mp_*`, `slot_sq_contains`, `slot_nb_bool`, `vectorcall_method`, the
  slot names of `slotdefs`): numeric and sequence slots other than
  `nb_bool`, `sq_length` and `sq_contains` have no thunk yet.
- `runtime/capi/object.c` from `Objects/call.c` (`_PyObject_MakeTpCall`,
  `PyObject_Call`, `PyObject_CallFunctionObjArgs` up to 16 arguments) and
  `Objects/object.c`/`abstract.c` (`PyObject_GetAttr`, `PyObject_SetAttr`,
  `PyObject_GetItem` over the mapping slot only).
- `lib/kernel/_boot.py`, `object.py`, `type.py`, `descriptors.py` and the
  `isinstance`/`issubclass` of `builtins.py` port to Python the C of
  `Python/bltinmodule.c` (`builtin___build_class__`, `update_bases`),
  `Objects/typeobject.c` (`slotdefs` and `fixup_slot_dispatchers`,
  `mro_implementation` and its C3 merge, `_PyType_CalculateMetaclass`,
  `type_call`, `type_new`, `type_init`, `type_getattro`, `type_setattro`,
  the type getsets, `object_new`, `object_init`, `object_repr`,
  `object_richcompare`, `object_set_class`, `slot_tp_getattr_hook`,
  `supercheck`, `do_super_lookup`, `super_init`, `super_getattro`),
  `Objects/object.c` (`_PyObject_GenericGetAttrWithDict`,
  `_PyObject_GenericSetAttrWithDict`, `PyObject_IsInstance`,
  `PyObject_IsSubclass`), `Objects/descrobject.c` (the descriptors'
  `__get__`/`__set__`, `property`, `mappingproxy`) and
  `Objects/funcobject.c` (`func_descr_get`, `classmethod`,
  `staticmethod`): messages are built with `str_join` and raised through
  `_lucifer.error` until f-strings and `raise` work in the kernel; a
  user `__getattribute__` that raises `AttributeError` reaches
  `__getattr__` once exception tables exist (K1k).
