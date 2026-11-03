No file of `Lib/` is modified.

`Parser/Python.asdl` and `Parser/asdl.py` are copied verbatim from the same tag;
tools/genast writes the node table of lib/modules/_ast.py from them.

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
- `runtime/prim/dtoa.c` from `Python/dtoa.c` (`_Py_dg_strtod`, `_Py_dg_dtoa`,
  `_Py_dg_freedtoa`): always the short-repr code; the headers are replaced by
  `lucifer.h`, `struct Bigint` is declared in the file, Bigints are allocated
  with `PyMem_Malloc` directly (the `Py_USING_MEMORY_DEBUGGER` paths: no
  per-interpreter freelist or cached powers of 5), and `_PyDtoa_Init` and
  `_PyDtoa_Fini` are dropped.
- `runtime/prim/pystrtod.c` from `Python/pystrtod.c` (`_Py_parse_inf_or_nan`,
  `PyOS_string_to_double`, `PyOS_double_to_string` over `format_float_short`):
  the `_PY_SHORT_FLOAT_REPR == 1` paths only; errors are raised with
  `lu_err_format` instead of `PyErr_Format`; no x87 precision control.
- `runtime/prim/float.c` from `Objects/floatobject.c` (arithmetic, `float_pow`,
  `float_richcompare` with ints, `double_round`, `hex`/`fromhex`,
  `as_integer_ratio`, `float_from_string_inner`) and
  `_Py_string_to_number_with_underscores`: operations take and return plain
  objects through primitives (NotImplemented for non-numbers), the libm error
  of `float_pow` is built without `PyErr_SetFromErrno`.
- `runtime/prim/complex.c` from `Objects/complexobject.c` (`_Py_c_*`,
  `_Py_cr_*`, `_Py_rc_*`, `c_powi`, the binary operators, `complex_repr`,
  `complex_hash`, `complex_richcompare`, `complex_from_string_inner`): no
  deprecation warnings (the warnings module does not exist yet).
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

## runtime/prim/int.c

Adapted from `Objects/longobject.c` (`x_add`, `x_sub`, `x_mul`, `k_mul`,
`k_lopsided_mul`, `x_divrem`, `inplace_divrem1`, `v_lshift`, `v_rshift`,
`long_pow` and `long_invmod`, the shifts, the two's complement bitwise
operations, `long_to_decimal_string_internal`, `long_format_binary`,
`PyLong_FromString` with `long_from_binary_base` and
`long_from_non_binary_base`, `_PyLong_AsByteArray`, `_PyLong_FromByteArray`,
`_PyLong_Frexp`, `PyLong_AsDouble`, `PyLong_FromDouble`,
`long_true_divide`) and `Python/sysmodule.c` (`get_int_max_str_digits`,
`set_int_max_str_digits`): the algorithms run over raw digit arrays with no
intermediate objects, results come back through `lu_long_from_digits`, the
`_pylong` module's subquadratic paths are omitted (they give the same
results), and `int()` of a str maps only ASCII digits and the common Unicode
spaces until the Unicode database exists. `runtime/prim/str.c`'s
`text_repr` stands in for `unicode_repr` and `bytes_repr` in the
`invalid literal` message with an approximate printable test for non-ASCII.
  `__getattr__` (a kernel try/except, running on the exception tables).
- `runtime/interp/exceptions.c` from `Objects/exceptions.c` (the layouts and
  attribute tables of `PyBaseExceptionObject` and the subtype structs,
  `PyException_*`), `Python/errors.c` (`_PyErr_SetObject`'s context chain,
  the handled-exception functions), `Python/traceback.c` (the traceback
  type, `PyTraceBack_Here`), `Python/ceval.c` (`get_exception_handler`) and
  `Objects/codeobject.c` (`PyCode_Addr2Line` by a forward scan of the
  location table). The frame a traceback holds is a record of the code,
  globals and instruction, not yet a frame object.
- `runtime/interp/ceval.c` from `Python/bytecodes.c` and `Python/ceval.c`:
  the `error:` and `exception_unwind:` labels, `RAISE_VARARGS`, `RERAISE`,
  `PUSH_EXC_INFO`, `POP_EXCEPT`, `CHECK_EXC_MATCH`, `CHECK_EG_MATCH`,
  `WITH_EXCEPT_START`, `LOAD_SPECIAL`, `LOAD_COMMON_CONSTANT` and
  `CALL_INTRINSIC_2` (`INTRINSIC_PREP_RERAISE_STAR`). `do_raise`, the
  special-method lookup and the except* matching are kernel functions the
  loop calls inline.
- `lib/kernel/exceptions.py` ports `Objects/exceptions.c` (`BaseException`
  and its subclasses' `__new__`, `__init__`, `__str__`, `__repr__`,
  `__reduce__`; `OSError`'s errno mapping, with Linux errno values;
  `BaseExceptionGroup`: `split`, `subgroup`, `derive`, `_PyExc_PrepReraiseStar`),
  `Python/ceval.c` (`do_raise`, `_PyEval_ExceptionGroupMatch`) and
  `Objects/typeobject.c` (`_PyObject_LookupSpecialMethod`). The
  `UnicodeError` family's `__str__` and `OSError.__str__` format integers and
  need `int` and `str` from the types that follow.
- `runtime/interp/errdisplay.c` from `Lib/traceback.py`
  (`TracebackException`, `StackSummary.format`, `_format_syntax_error`) and
  `Python/pythonrun.c` (`_Py_HandleSystemExit` in `runtime/pylifecycle.c`):
  the default excepthook's output without source lines, carets or
  suggestions, until the traceback module itself is ported.
- `lib/kernel/_listsort.py` from `Objects/listobject.c` (`list_sort_impl`
  and its helpers: the pre-sort comparison choice, `count_run`,
  `binarysort`, `gallop_left`, `gallop_right`, `merge_lo`, `merge_hi`,
  `merge_at`, `powerloop`, `found_new_run`, `merge_force_collapse`,
  `merge_compute_minrun`). CPython 3.14 merges by powersort, where PyPy's
  `listsort.py` keeps the older stack invariants, so comparison counts follow
  this port. A key sorts indexes into the key list, so keys never move.
- `runtime/prim/storage.c` from `Objects/listobject.c` (`list_resize`,
  `list_ass_slice_lock_held`, the extended-slice delete loop of
  `list_ass_subscript`, `list_sort_impl`'s emptying of the list) and
  `Objects/rangeobject.c` (`range_iter`, `fast_range_iter`): layouts and
  storage moves only, each list mutator inside the list's critical section.
- `lib/kernel/{tuple,list,range,slice,iter}.py` port `Objects/tupleobject.c`,
  `Objects/listobject.c`, `Objects/rangeobject.c`, `Objects/sliceobject.c`,
  `Objects/iterobject.c` and the sequence and iteration parts of
  `Objects/abstract.c` (`PyObject_GetItem`, `SetItem`, `DelItem`,
  `PyObject_GetIter`, `_PySequence_IterSearch`, `unpack_iterable`,
  `_PyEval_ExtendList`) with `PySlice_Unpack` and `PySlice_AdjustIndices`.
- `runtime/interp/ceval.c`: `STORE_SUBSCR`, `DELETE_SUBSCR`, `BUILD_SLICE`,
  `BINARY_SLICE`, `STORE_SLICE`, `UNPACK_EX`, `GET_LEN` and the general
  `GET_ITER`, `FOR_ITER`, `UNPACK_SEQUENCE`, `LIST_EXTEND` and
  `BINARY_OP` `NB_SUBSCR` from `Python/bytecodes.c`, with the exact list,
  tuple and range fast paths.
- `runtime/prim/unicodetype_db.h` is `Objects/unicodetype_db.h` unchanged;
  `runtime/prim/unicodedata.c` is `Objects/unicodectype.c` with the `Python.h`
  and table includes pointed at `runtime/prim`. They are the database behind
  the character predicates, case mappings and `repr` of `str`.
- `runtime/prim/str.c` ports the loops of `Objects/unicodeobject.c` (`ADJUST_INDICES`,
  `find`/`rfind`/`count`/`replace`, `split`/`rsplit`/`splitlines`, `do_strip`,
  `handle_capital_sigma`, `do_upper_or_lower`, `do_title`, `do_capitalize`,
  `do_swapcase`, `do_casefold`, `unicode_isalpha` and its siblings,
  `unicode_expandtabs`, `unicode_repr`, `unicode_join`, `unicode_subtype_new`'s
  non-compact layout) and of `Python/codecs.c` and `Objects/stringlib/codecs.h`
  (the utf-8 decoder's error classes, the `strict`, `ignore`, `replace`,
  `backslashreplace`, `xmlcharrefreplace`, `surrogateescape` and
  `surrogatepass` handlers for utf-8, latin-1 and ascii). Results are built in
  a UCS4 buffer and narrowed by `PyUnicode_FromKindAndData`; there is no
  fast-search library (`memmem` for one-byte kinds, a scan otherwise); the
  `namereplace` handler and codecs other than the three are absent.
- `lib/kernel/_format.py` ports `Python/formatter_unicode.c` (the spec parser,
  `calc_number_widths`, `fill_number`, `format_string_internal`,
  `format_long_internal`; the float and complex paths are not ported),
  `Python/thousands_grouping` (`InsertThousandsGrouping`),
  `Objects/stringlib/unicode_format.h` (`str.format`, `format_map`) and
  `Objects/unicodeobject.c` (`PyUnicode_Format`; `%e`/`%f`/`%g` become a format
  spec for `float.__format__`), plus `PyObject_Str`, `PyObject_Repr`,
  `PyObject_ASCII` and `PyObject_Format`. `lib/kernel/str.py` ports the argument
  handling and messages of the `str` methods and `unicode_new_impl`.
- `lib/modules/_io.py` is `Lib/_pyio.py` made the builtin `_io`. The base
  classes are `_IOBase`, `_RawIOBase`, `_BufferedIOBase` and `_TextIOBase`
  without `abc` (`io.py` derives the abstract ones); the file layer is the
  posix port (no `os`, `stat`, `locale`; `readinto` is a read and a copy);
  `BufferedReader`, `BufferedWriter` and `BufferedRandom` derive from
  `_BufferedIOBase` alone, as in C, and borrow the shared methods; the raw
  reads and writes of `read`, `read1`, `peek`, `readline` and `write` follow
  `Modules/_io/bufferedio.c`; `TextIOWrapper` keeps encoded text pending up to
  its chunk size as `textio.c` does, encodes utf-8, ascii and latin-1 without
  an encoder object, and takes its error messages from C; `StringIO` is its
  own class after `stringio.c`; the closed-file and unsupported-operation
  messages are C's; `open`, `open_code` and `text_encoding` are builtin
  functions; the unclosed-file `ResourceWarning` path is unchanged.
- `lib/modules/_codecs.py` ports `Python/codecs.c` (the search registry, the
  error handlers, `PyCodec_Lookup` normalization) and `Modules/_codecsmodule.c`
  with the decoders and encoders of `Objects/unicodeobject.c` for utf-16,
  utf-32, the escape codecs and charmap; utf-8, latin-1 and ascii run on the
  string primitives. utf-7 is absent.
- `lib/kernel/builtins.py` `import_star` is `import_all_from` of
  `Python/ceval.c`.
- `lib/kernel/genericalias.py` ports `Objects/genericaliasobject.c` and
  `Objects/unionobject.c` (types.GenericAlias, typing.Union, `type | type`,
  the `__class_getitem__` of the builtin generics) and runs the PEP 695
  intrinsics through `lib/modules/_typing.py`, a port of
  `Objects/typevarobject.c` (type parameters, aliases and `Generic`; the bound,
  constraints, default and alias value are evaluated on first use).
  `lib/kernel/mappingproxy.py` ports the methods of `mappingproxy` and the
  `mapping` of the dict views (`Objects/descrobject.c`, `Objects/dictobject.c`).
  `lib/modules/_types.py` is `Modules/_typesmodule.c`.
- `lib/modules/_collections.py` (deque, defaultdict, `_count_elements`,
  `_tuplegetter`) and `lib/modules/itertools.py` follow
  `Modules/_collectionsmodule.c` and `Modules/itertoolsmodule.c`; the deque is
  a list and the itertools classes cannot be pickled yet.
- `runtime/prim/ucd.c` is `Modules/unicodedata.c` without its Python bindings:
  the database lookups, decomposition, the NFC/NFD/NFKC/NFKD algorithms and the
  name and lookup code (DAWG) are kept, each lookup a primitive whose first
  argument selects the 3.2.0 database; `unicodedata_db.h` and
  `unicodename_db.h` are copied verbatim into `runtime/prim/`.
  `PyOS_mystrnicmp` and `PyOS_mystricmp` are `Python/pystrcmp.c`.
  `lib/modules/unicodedata.py` supplies the argument handling and messages.
- `runtime/prim/math.c` holds `PyFloat_Pack2` and `PyFloat_Unpack2`
  (`Objects/floatobject.c`) for `lib/modules/_struct.py`; `lib/modules/math.py`
  follows `Modules/mathmodule.c` (`math_1`/`math_2` error rules, `m_tgamma`,
  `m_lgamma`, `vector_norm`, `fsum`, the factorial and isqrt algorithms, `nextafter`)
  over the libm primitives. `lib/modules/_struct.py`, `_random.py` (MT19937),
  `_contextvars.py`, `_templatelib.py` (`Objects/templateobject.c`,
  `Objects/interpolationobject.c`), `_opcode.py`, `_blake2.py`, `_sha3.py`,
  `pyexpat.py` and `faulthandler.py` are Python ports of the C modules of the
  same names; `_md5.py`, `_sha1.py`, `_sha2.py`, `_sha256.py` and `_sha512.py`
  are PyPy's `lib_pypy` modules with `name` attributes added.
- `runtime/prim/os.c` gains `fork_exec`, the child half of
  `Modules/_posixsubprocess.c` (`child_exec` without `preexec_fn`), and
  `lib/modules/posix.py` the `posix` functions `os`, `shutil`, `tempfile` and
  `subprocess` use.
- `lib/kernel/match.py` is `_PyEval_MatchClass` and `match_keys` of
  `Python/ceval.c`; `lib/kernel/cell.py` is `Objects/cellobject.c`.
- `lib/modules/sysconfigdata.py` is the Makefile and `pyconfig.h` values of
  the free-threaded 3.14 ABI, installed as `_sysconfigdata_t_linux_x86_64-linux-gnu`.
