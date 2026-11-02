# The object model forced by the CPython 3.14 C API

Research date: 2026-11-02. Design input for human review; no implementation, builds, benchmarks, or NumPy tests were run. The fixed requirements are full non-limited C API, an unmodified NumPy build passing its own suite, no GIL, and built-ins/stdlib mostly implemented in Python over native primitives (`README.md`). **Recommendation: choose the CPython 3.14 free-threaded ABI (`cp314t`), start with canonical nonmoving C-compatible heap objects, and recover unboxing through JIT virtualization before escape.** A CPython-compatible header alone is insufficient: concrete payloads, static objects, ownership, locking, and callback conventions also matter. The evidence and the decisions this recommendation leaves open follow.

## 1. Evidence, scope, and reproducibility

| Source | Pinned checkout / scope |
| --- | --- |
| CPython | `vendor/cpython`, tag `v3.14.0`, commit `ebf955df7a89ed0c7968f79faec1de49f61ed7cb`; version in `Include/patchlevel.h:20`; 284 files under `Include`, including 282 `.h` files. |
| NumPy | `vendor/numpy`, commit `df9913aa4ea20c33728fbc434acc66c3285f12a2` (2026-10-31); development snapshot, not a numbered release. Counts include newer-version conditional branches; they are not measurements of a CPython 3.14 build. |
| PyPy | `vendor/pypy3`, branch `py3.12`, commit `2e890abd7377b8dc1f5e94064e91c9bbfd8ddcf0`; targets Python 3.12.15 / PyPy 8.0.2 alpha (`pypy/module/sys/version.py`). Inspect `pypy/module/cpyext`, `rpython/rlib/rawrefcount.py`, and `pypy/doc/discussion/rawrefcount.rst`. |
| The Smalltalk VM | Only read-only source blobs and tree inventories at `origin/master` used. `docs/vm/flat-layout-direction-PLAN.md` distinguishes identity, representation, and lifetime, with rematerialization at identity observation. Its staged proposals are not evidence that every listed optimization is implemented. |
| Web | Primary project documentation, source, PEPs, and authors' historical measurements, linked at each claim. Moving branch URLs describe the versions inspected on the research date; Cython examples are pinned to 3.1.4. |

The header census in Appendix A scans **every `.h` under `Include`, including `cpython/`, `internal/`, and bundled mimalloc headers**. After masking comments and string/character literals, it records multiline macros and `static inline` / `Py_LOCAL_INLINE` bodies containing member access (`->field` or `.field`). It adds the fieldless header initializers/declarations, Unicode payload arithmetic/read/write, and GC prefix arithmetic explicitly. Result: **526 syntactically field-bearing definitions, plus 13 explicit layout-related definitions = 539 definitions / 507 distinct identifiers in 90 headers**. Conditional definitions are retained separately. Appendix A lists each definition, line, and member names; Appendix B also lists wrappers/aliases whose expansion reaches these operations.

This is a source inventory, **not a preprocessor-selected ABI certification**: debug, statistics, platform, GIL, and free-threaded alternatives are all present; assertion-only field reads are counted. A member-bearing macro can also generate a function. Configuration initializers and allocator helpers are included for completeness without claiming that extensions access runtime objects through them. The one explicitly spelled C++ non-static inline in `Include/dynamic_annotations.h:473` reads a scalar through a pointer, not a struct member; implicit inline C++ allocator methods were reviewed separately after Appendix A and are outside the 539-definition census. Public declarations also expose layouts without providing any accessor; §2 covers that separate obligation. The scanner and count rules are reproduced in Appendix D; active generated code and binary imports remain **unverified**.

The API boundaries are important: `Include/README.rst` identifies `Include/cpython/` as implementation details, `PyUnstable_*` as unstable API, and `Include/internal/` plus underscore-prefixed names as internal API. Normal non-limited `Python.h` includes many `cpython/` layouts, including `longintrepr.h` (`Include/Python.h:76–148`). Most `pycore_*` headers explicitly require `Py_BUILD_CORE`. **Full non-limited extension support does not automatically promise the whole CPython interpreter's private optimizer/runtime structures.** They are nevertheless inventoried here so an actual private-header dependency cannot be mistaken for an opaque public API.

## 2. What a compiled extension fixes in machine code and static data

### 2.1 Header, ownership, and identity

An extension can embed offsets, load/store instructions, field widths, atomic memory orders, masks, array strides, struct sizes, designated/positional initializers, and function-pointer calling conventions. Supplying a symbol named like a macro cannot intercept code already compiled from it. This follows directly from the inline definitions in `Include/object.h`, `Include/refcount.h`, and the concrete object headers below.

Expected little-endian x86-64 layouts, **derived from declarations and normal AMD64 alignment, not compiled probes**:

| Property | Ordinary `cp314` | Free-threaded `cp314t` | Evidence |
| --- | --- | --- | --- |
| `PyObject` | 16 bytes: refcount union at +0; `ob_type` +8 | 32 bytes: `ob_tid` +0, `ob_flags` +8, `ob_mutex` +10, `ob_gc_bits` +11, `ob_ref_local` +12, `ob_ref_shared` +16, `ob_type` +24 | `Include/object.h:109–163`; `Include/cpython/lock.h` |
| Ordinary refcount union | On 64-bit little-endian: 32-bit `ob_refcnt`, 16-bit overflow, 16-bit flags; also 64-bit `ob_refcnt_full` | Separate 32-bit local and signed shared count | Same definitions; assuming the old entire first word is just a signed `Py_ssize_t` loses 3.14 detail. |
| `PyVarObject.ob_size` | +16; base size 24 | +32; base size 40 | `Include/object.h:169–172` |
| `Py_TYPE` / `_Py_TYPE`, `Py_SET_TYPE` | Direct type load/store | Direct type load/store at different offset | `Include/object.h:264–305` |
| `Py_SIZE`, `Py_SET_SIZE` | Direct variable-size load/store | Same size location rule; setter uses relaxed atomic store | `Include/object.h:283–317`; not the representation of `int`/`bool`. |
| `PyObject_HEAD_INIT`, `PyVarObject_HEAD_INIT` | Initializes immortal refcount/flags/type in static data | Initializes owner/flags/mutex/GC/local/shared/type fields in static data | `Include/object.h:65–96`; runtime allocation hooks cannot repair wrong static bytes. |

`Py_REFCNT` / `_Py_REFCNT`, `_Py_IsImmortal`, `_Py_IsStaticImmortal`, `Py_SET_REFCNT`, `Py_INCREF`, and the conditional `Py_DECREF` implementations touch the header. The affected wrappers include `Py_XINCREF`, `Py_XDECREF`, `Py_NewRef`, `Py_XNewRef`, `Py_CLEAR`, `Py_SETREF`, `Py_XSETREF`, singleton return macros, and object/type-check macros (`Include/refcount.h`; Appendix B). `Py_Is` also compares pointers directly (`Include/object.h:179`): a raw C pointer must represent a stable identity, not an arbitrarily changeable forwarding address.

On ordinary 64-bit 3.14, immortality is encoded using the low 32-bit count, with `_Py_IMMORTAL_MINIMUM_REFCNT = 2**31` and initial count `3 << 30`, plus static flag bits. Free-threaded immortality is `ob_ref_local == UINT32_MAX`. Free-threaded shared counts are shifted by two with low-bit states `INIT`, `MAYBE_WEAKREF`, `QUEUED`, `MERGED`; inline INCREF selects owner-local relaxed operations or shared atomic increments. DECREF can call `_Py_DecRefShared` / `_Py_MergeZeroLocalRefcount`; ordinary DECREF calls `_Py_Dealloc` at zero (`Include/refcount.h:19–95,105–192,250–426`). These are semantic obligations, not spare padding.

### 2.2 Concrete layouts and accessor families

The following is the interpretation of the exhaustive symbol inventory, including transitive access through base-header/type checks. Header paths in this table are below `vendor/cpython/Include/`; `Doc/` paths are below `vendor/cpython/`.

| Layout / operation | Baked-in contract | Headers |
| --- | --- | --- |
| Tuple | `PyTuple_GET_SIZE`, `GET_ITEM`, `SET_ITEM`; variable header, **3.14 cached `ob_hash` before inline `ob_item[]`**. Items are real owning `PyObject*` entries; SET steals the supplied reference and is for construction. Expected item-array offsets +32 / +48 for ordinary / t ABI. | `cpython/tupleobject.h:5–40`; `Doc/c-api/tuple.rst` |
| List | `PyList_GET_SIZE`, `GET_ITEM`, `SET_ITEM`; pointer to separately allocated `PyObject** ob_item`, `allocated`, and variable-header length. GET_ITEM remains a naked array read even in t builds; SET_ITEM does not decref a replaced entry or install a GC write barrier. Expected object sizes 40 / 56 bytes. | `cpython/listobject.h:5–52`; `Doc/c-api/list.rst` |
| Fast sequences | `PySequence_Fast_GET_SIZE`, `GET_ITEM`, `ITEMS` select list/tuple; ITEMS exposes their actual pointer array. `PySequence_ITEM` directly follows `tp_as_sequence->sq_item`. | `cpython/abstract.h:82–104` |
| Bytes | `PyBytes_AS_STRING` returns inline `ob_sval`; `GET_SIZE` reads base length. Layout also includes deprecated `ob_shash` and trailing NUL. Expected payload offsets +32 / +48; an external data pointer in place of inline bytes fails the accessor. | `cpython/bytesobject.h:5–35` |
| Bytearray | `AS_STRING` uses `ob_start` or the shared empty string; `GET_SIZE` loads size, atomically in t builds. Layout additionally exposes allocation, base buffer, and export count. | `cpython/bytearrayobject.h:5–39` |
| Unicode | `CHECK_INTERNED`, `IS_ASCII`, `IS_COMPACT`, `IS_COMPACT_ASCII`, `KIND`, `DATA`, `1/2/4BYTE_DATA`, `GET_LENGTH`, `READ_CHAR`, `MAX_CHAR_VALUE`, `READ`, `WRITE`; payload address and code-unit width are compiled into callers. `_PyUnicodeWriter_Prepare` / `PrepareKind` inspect writer fields too. | `cpython/unicodeobject.h:48–396,553–575` |
| Float | `PyFloat_AS_DOUBLE` reads `double ob_fval` immediately after `PyObject`; expected sizes 24 / 40 bytes. | `cpython/floatobject.h:5–18` |
| Int | **Yes, int belongs in the concrete-layout set.** `PyLongObject` has `long_value.lv_tag` and `ob_digit[]`, not a signed `ob_size`. `_PyLong_IsCompact` / `CompactValue` and their `PyUnstable_Long_*` aliases inspect it inline. Tag low bits encode sign/small-int state; usual digits are 30 bits in `uint32_t`, with a supported 15-bit configuration. | `cpython/longintrepr.h:11–140`; Cython evidence in §4 |
| Dict / set | `PyDict_GET_SIZE` reads `ma_used`; exposed dict has watcher tag, keys/values pointers. `PySet_GET_SIZE` reads `used`; exposed set includes table, mask, cached hash, small table, weakrefs. Internal `DK_*` also depend on key-table layout. | `cpython/dictobject.h:5–63`; `cpython/setobject.h:20–71`; `internal/pycore_dict.h` |
| Cells, methods, functions | `PyCell_GET/SET`; `PyMethod_GET_FUNCTION/SELF`, `PyInstanceMethod_GET_FUNCTION`; `PyCFunction_GET_FUNCTION/SELF/FLAGS/CLASS`; `PyFunction_GET_CODE/GLOBALS/MODULE/DEFAULTS/KW_DEFAULTS/CLOSURE/ANNOTATIONS`. Direct object fields and method-definition pointers, not generic attribute lookups. | `cpython/{cellobject,classobject,methodobject,funcobject}.h` |
| Code / descriptors | `PyCode_GetNumFree`, `PyUnstable_Code_GetFirstFree` / legacy alias; `PyDescr_TYPE/NAME`. `PyCodeObject` itself is defined by `_PyCode_DEF`, including exposed metadata and code array. | `cpython/{code,descrobject}.h` |
| Weakrefs | Deprecated `PyWeakref_GET_OBJECT` reads `wr_object` and checks refcount; a borrowed result is not generally safe against concurrent destruction. Free-threaded weakrefs have a lock pointer. | `cpython/weakrefobject.h:7–66` |
| Memoryview / buffer | `PyMemoryView_GET_BUFFER` returns an address **inside** the view; GET_BASE reads its `view.obj`. The public `Py_buffer` has fixed field layout and an owned exporter reference; buffer contents/metadata must remain valid for the export. | `cpython/memoryobject.h:8–49`; `pybuffer.h:10–34` |
| Datetime | Every `PyDateTime_*GET_*`, `PyDateTime_DELTA_GET_*`, `_PyDateTime_HAS_TZINFO` and C-API-table construction/check wrapper in Appendix A exposes packed date/time bytes, fold/tzinfo, delta fields, or table slots. This header is explicitly imported, not included by `Python.h`. | `datetime.h:17–259` |
| Locks / monitoring / configuration | `_PyMutex_Lock/Unlock/IsLocked` touch mutex byte; monitoring wrappers test `state->active`; initializer macros define compiler/interpreter configuration field values. They are counted even though they are not builtin payload accessors. | `cpython/{lock,monitoring,compile,pylifecycle}.h` |

Unicode merits an explicit layout constraint. `PyASCIIObject` contains header, code-point length, hash, and state; compact ASCII characters immediately follow `sizeof(PyASCIIObject)`. Non-ASCII compact strings use the larger `PyCompactUnicodeObject` (adds UTF-8 length/cache pointer), with character data immediately after that struct. Noncompact strings/subclasses use `PyUnicodeObject.data.any`. Kind is 1, 2, or 4 bytes per code point; this is not interchangeable with an arbitrary UTF-8-only body. The free-threaded `state.interned` is a separate atomically accessed byte instead of the ordinary build's bitfield. `PyUnicode_IS_READY` and `PyUnicode_READY` are compatibility no-ops here: do not recreate an obsolete deferred-ready/wstr layout (`cpython/unicodeobject.h:48–181,209–301,407–415`). The returned character/UTF-8 buffers require stable lifetimes, even if high-level string algorithms are Python (`Doc/c-api/unicode.rst`).

### 2.3 Types, extension-defined payloads, vectorcall, and GC

`PyType_HasFeature` reads `tp_flags` directly for non-limited builds (`Include/object.h:776`). Non-limited code can also initialize, mutate, and call **every exposed type slot directly**, independently of macros. The layout includes allocation sizes; deallocation, finalization, call/vectorcall, repr/hash/comparison, attribute, descriptor, iterator, traversal/clear, allocator/free hooks; number/sequence/mapping/async/buffer tables; bases, dict/MRO, methods/members/getsets, offsets, and version/watch fields. A substitute type representation must provide the exact `PyTypeObject`, `PyHeapTypeObject`, and method-table prefix layouts and meaningful flags (`Include/cpython/object.h:61–291`). `Py_TPFLAGS_*_SUBCLASS`, `HAVE_GC`, `HAVE_VECTORCALL`, managed-dict/weakref and preheader flags cannot be optimistic declarations: callers use them to select layouts.

An extension can define `struct { PyObject_HEAD; ... }`, allocate it itself, embed a builtin as its base, use `sizeof`/`offsetof`, and publish offset-based members. Thus the layout obligation includes extension objects and builtin subclasses, not just VM-allocated exact builtins (`Include/objimpl.h:20–67`; `Include/descrobject.h`, `structmember.h`). NumPy's `PyUnicodeScalarObject` literally embeds `PyUnicodeObject`; numeric scalar structs contain a header followed by native numeric values (`vendor/numpy/numpy/_core/include/numpy/arrayscalars.h:5–166`). Other directly exposed structures include `PyComplexObject.cval`, `PySliceObject.start/stop/step`, traceback links/fields, exception structs, `PyMethodDef`, `PyModuleDef`, and `PyMemberDef`, even where no specialized macro reads them (`Include/cpython/{complexobject,traceback,pyerrors}.h`; `Include/{sliceobject,methodobject,moduleobject,descrobject}.h`). The small proposed hybrid whitelist is therefore not closed.

Public `PyVectorcall_Function` is **an out-of-line declaration in this version**, not a public direct-field macro; `PyVectorcall_NARGS` is just a bit-mask inline. The private `_PyVectorcall_FunctionInline` tests flags, reads `tp_vectorcall_offset`, and copies a function pointer from `(char*)callable + offset` (`Include/cpython/abstract.h:30–42`; `Include/internal/pycore_call.h:115–135`). Extensions can perform equivalent arithmetic themselves and initialize `tp_vectorcall_offset`/per-instance vectorcall fields. Lucifer must implement the C calling convention, argument array, keyword-name tuple, and `PY_VECTORCALL_ARGUMENTS_OFFSET` permission to use `args[-1]`; slots can be native thunks into JIT-compiled Python, with correct ownership/error handling (`Include/internal/pycore_call.h:139–179`; `Include/cpython/methodobject.h:5–14`).

The ordinary GC prefix is a `PyGC_Head` with tagged next/previous words; `_Py_AS_GC` subtracts its size, and private GC inlines manipulate its bits and links. At x86-64 this is a derived 16-byte prefix, **not part of every object and not a universal free-threaded prefix** (`Include/internal/pycore_interp_structs.h:157–169`; `pycore_gc.h:17–25,161–207`). In t builds, tracking/finalization is in `ob_gc_bits`; `_PyType_PreHeaderSize` omits `PyGC_Head` but can still reserve two pointers for `Py_TPFLAGS_PREHEADER` (`Include/internal/pycore_object.h:854–862`). Public `PyObject_GC_New/Track/UnTrack/Del`, traversal, clear, and finalization must work; internal generation-list mechanics need not be copied unless those private contracts are promised (`Include/objimpl.h`; `Python/gc_free_threading.c`).

Raw C stores provide **no JIT write barrier or deopt notification**. Consequently, a collector must account for extension-owned edges through ownership and `tp_traverse`; a moving/generational collector cannot assume all stores pass through Lucifer primitives. This is a design deduction from `PyTuple_SET_ITEM`, `PyList_SET_ITEM`, offset-based members, and `tp_traverse`, not an implemented collector design.

## 3. What NumPy actually uses

Appendix C gives every nonzero identifier from the extension-facing header closure: **104 identifiers**, with separate C, C++, and header counts. Scope is the whole checkout, including tests and bundled dependencies: **1,131 `.c`/`.c.src`, 396 `.cpp`/`.cpp.src`/`.cc`/`.cxx`, and 663 `.h`/`.h.src`/`.hpp`/`.hpp.src` files**. Counts are whole-token source occurrences after masking comments/literals; preprocessor branches and macro definitions remain. Templates count once, before generation. They are neither call counts nor a dynamic frequency profile. Raw C grep totals are included where comments change the result.

| Identifier | C code tokens | Raw C tokens | C excluding bundled `pythoncapi-compat/` | Interpretation |
| --- | ---: | ---: | ---: | --- |
| `Py_INCREF` | 594 | 595 | 588 | Ownership updates are pervasive. |
| `Py_DECREF` | 1,941 | 1,946 | 1,786 | Includes zero-count callbacks, not just bookkeeping. |
| `Py_XDECREF` | 718 | 719 | See Appendix C for all-file counts | Nullable cleanup. |
| `PyTuple_GET_ITEM` / `GET_SIZE` / `SET_ITEM` | 119 / 88 / 99 | 119 / 88 / 99 | GET_ITEM: 118 | Concrete tuple storage is unavoidable. |
| `PyList_GET_ITEM` / `GET_SIZE` / `SET_ITEM` | 8 / 7 / 6 | 9 / 8 / 6 | GET_ITEM: 8 | Direct array operations. |
| `PySequence_Fast_ITEMS` | 8 | 8 | See Appendix C | Raw list/tuple array exposure. |
| `PyBytes_AS_STRING` / `GET_SIZE` | 8 / 8 | 8 / 8 | See Appendix C | Inline bytes payload/length. |
| `PyFloat_AS_DOUBLE` | 5 | 5 | 5 | Plus 2 C++ occurrences. |
| `Py_TYPE` / `Py_REFCNT` | 182 / 40 | 184 / 42 | REFCNT: 3 | Most count tests here are bundled compatibility tests; production still depends on uniqueness. |
| `PyUnstable_Object_IsUniquelyReferenced` / `IsUniqueReferencedTemporary` | 4 / 1 | 5 / 1 | 2 / 1 | Out-of-line APIs, additional to macro closure. |

The Unicode C counts are `PyUnicode_1BYTE_DATA`, `2BYTE_DATA`, `4BYTE_DATA`: **1 each**; `GET_LENGTH`: **2**; `IS_ASCII`: **3**; `KIND`: **2**; `READ_CHAR`: **1**. `PyUnicode_DATA` is **0 C / 1 header**, not a missing dependency: typed-data macros expand through it (`_core/src/multiarray/textreading/stream_pyobject.c:74–81`; `scalartypes.c.src:3825`; Appendix C). Date/time getters/construction are present in `_core/src/multiarray/datetime.c:2130–2140,2255,2785–2789,2942–2947`; memoryview accessors appear at `ctors.c:1199` and `conversion_utils.c:334`. GC allocation/tracking/destruction appears in `_core/src/umath/ufunc_object.c:5405,5538,5952,5971`.

Counting raw field names adds obligations hidden by an API-only grep:

| Field token | C / C++ / header counts | Qualification / example |
| --- | ---: | --- |
| `ob_type` | 5 / 0 / 1 | Four C occurrences outside bundled compatibility sources; e.g. `methods.c:2091–2092`, `stringdtype/dtype.c:1161`, `umath/override.c:78`. |
| `ob_refcnt` | 3 / 0 / 2 | Two C occurrences are disabled debug tracing in `dtype_transfer.c:48–61`; the third is a compatibility test. Do not call these active t-build operations. |
| `ob_tid`, `ob_ref_local`, `ob_ref_shared` | 1 / 0 / 1; 1 / 0 / 4; 1 / 0 / 6 | C occurrences initialize a compatibility test object (`pythoncapi-compat/tests/test_pythoncapi_compat_cext.c:2516–2523`); header backports are conditional. |
| `ob_item`, `ob_fval`, `ob_sval`, `ob_shash`, `ob_hash` | 0 / 0 / 0 each | No literal accesses in counted NumPy files; macro expansion still requires them. |
| `lv_tag`, `ob_digit` | 0 / 0 / 2 each | In bundled integer backports guarded by `PY_VERSION_HEX < 0x030E00A2`; inactive on 3.14.0 (`pythoncapi_compat.h:1876–1909`). |
| `tp_flags`, `tp_basicsize`, `tp_itemsize` | 15 / 0 / 0; 22 / 0 / 0; 4 / 0 / 0 | Includes initializers and mutations, e.g. `scalartypes.c.src:4670–4682`. |
| `tp_dealloc`, `tp_as_number`, `tp_as_sequence` | 21 / 0 / 0; 24 / 0 / 2; 7 / 0 / 0 | NumPy initializes and directly calls slots (`scalartypes.c.src:499–515,653–656`). |
| `tp_vectorcall_offset`, `tp_dictoffset`, `tp_weaklistoffset` | 1 / 0 / 0 each | Layout-sensitive offsets, independent of accessor macros. |

Exact-count assumptions require special care. Production `Py_REFCNT` uses are `shape.c:104,115` (the latter is the old-version branch) and `descriptor.c:2156`. On 3.14, resize uses `PyUnstable_Object_IsUniquelyReferenced`; count 2 selects a particular false-positive diagnostic. `temp_elide.c:114–128` uses `PyUnstable_Object_IsUniqueReferencedTemporary`, which in CPython also examines interpreter-stack ownership (`vendor/cpython/Objects/object.c:2619–2641`). `descriptor.c:2149–2164` intentionally uses count 1 for a non-GC, non-weakref descriptor during deallocation. A proxy bias cannot simply be declared observationally irrelevant: it can lose an optimization, change a diagnostic, or reject a resize that another runtime accepts. Returning false from uniqueness queries may be memory-safe, but is not evidence of the required test-suite behavior.

NumPy declares `Py_MOD_GIL_NOT_USED` (`_core/src/multiarray/multiarraymodule.c:5788–5796`) and wraps fast-sequence list access in critical sections for t builds (`_core/src/common/npy_pycompat.h:50–85`). These establish that an upstream free-threaded NumPy build is a concrete interpretation of “unmodified NumPy.” They do not establish universal safety of unsynchronized ndarray mutation: NumPy documents resize/mutation races and extra object-dtype risks (`doc/source/reference/thread_safety.rst`).

## 4. Cython: generated binaries have additional layout dependencies

No counted NumPy C/C++/header file contains `CYTHON_`; generated Cython outputs are not in this source census. NumPy nevertheless requires Cython >=3.1.0 (`vendor/numpy/pyproject.toml:5`, `meson.build:3,21,36`). Therefore the census **understates the generated code of an actual build**; that generated-code/binary audit remains unverified, and was not replaced by a build in this research phase.

Pinned Cython source demonstrates the missing patterns:

| Pattern | Verified source |
| --- | --- |
| List append fast path checks capacity, stores `L->ob_item[len]`, and updates size; comprehension paths make equivalent assumptions. | [Cython 3.1.4 `Optimize.c:28–71`](https://github.com/cython/cython/blob/3.1.4/Cython/Utility/Optimize.c#L28) |
| Tuple/list construction copies directly into `ob_item`; slicing reads the array. Some branches are gated by `CYTHON_COMPILING_IN_CPYTHON`, rather than `CYTHON_USE_PYLIST_INTERNALS`; list slicing uses a critical section. | [Cython 3.1.4 `ObjectHandling.c:775–853`](https://github.com/cython/cython/blob/3.1.4/Cython/Utility/ObjectHandling.c#L775) |
| Integer fast paths read `long_value.lv_tag` and `long_value.ob_digit` for >=3.12. | [Cython 3.1.4 `TypeConversion.c:120–165`](https://github.com/cython/cython/blob/3.1.4/Cython/Utility/TypeConversion.c#L120) |
| Free-threaded configuration disables list-internals optimizations and unsafe borrowed-reference paths but retains long, Unicode, and type-slot internals. | [Cython 3.1.4 `ModuleSetupCode.c:259–380`](https://github.com/cython/cython/blob/3.1.4/Cython/Utility/ModuleSetupCode.c#L259) |

**Can Cython be told to use only functions? No universal function-only guarantee was verified.** Compile-time controls such as `CYTHON_USE_TYPE_SLOTS=0`, `CYTHON_USE_PYLONG_INTERNALS=0`, `CYTHON_USE_PYLIST_INTERNALS=0`, `CYTHON_USE_UNICODE_INTERNALS=0`, `CYTHON_ASSUME_SAFE_MACROS=0`, `CYTHON_ASSUME_SAFE_SIZE=0`, and `CYTHON_AVOID_BORROWED_REFS=1` reduce assumptions; they require regenerating/recompiling, and one flag does not cover every path. See [documented Cython controls](https://docs.cython.org/en/latest/src/userguide/source_files_and_compilation.html) and the independently gated `ObjectHandling.c` paths above.

`Py_LIMITED_API` is the closest enforced restricted-header mode, supported usefully from Cython 3.1 with restrictions on builtin subclassing, `cimport cpython`, tracing, and related features. Cython's own compiler reportedly gains about 35% when compiled with the regular API versus 0–10% with Limited API, relative to running it uncompiled: a workload-specific lost optimization, not a universal ABI tax ([Cython 3.1.4 Limited API documentation](https://github.com/cython/cython/blob/3.1.4/docs/src/userguide/limited_api.rst)). Defining only `CYTHON_LIMITED_API` chooses Cython's restricted-style paths without enforcing the headers (`ModuleSetupCode.c:2–6`). Even CPython's Limited API is not literally all function calls: this 3.14 header still inlines `Py_SIZE`, `Py_SET_TYPE`, and `Py_SET_SIZE` (`Include/object.h:283–317`). Moreover, 3.14 rejects `Py_LIMITED_API` with `Py_GIL_DISABLED` (`Include/Python.h:48–52`). None of these switches changes an existing full-API NumPy binary or satisfies the owner's requirement by substituting a limited-API package.

## 5. Prior art: what transfers, and what does not

### 5.1 PyPy cpyext, read from the implementation

All implementation paths in this subsection are relative to `vendor/pypy3`. `as_pyobj` lazily calls `create_ref`, which allocates and attaches a C representation. Allocation sets `ob_refcnt` to `REFCNT_FROM_PYPY + 1`; `create_ref` links it and removes that extra reference. `track_reference` links prefixed objects to `W_Root`, while prefix-less C objects (including statically allocated NumPy dtypes) use address-keyed out-of-band maps; that path retains an extra reference to prevent address reuse. `from_ref` looks up either mapping or realizes a managed representation. `as_pyobj` returns a borrowed native pointer valid while the managed object stays alive, with an explicit `keepalive_until_here` requirement (`pypy/module/cpyext/pyobject.py:86–128,158–193,290–342,414–504`). This combines native ownership with tracing, rather than counting every managed reference. `make_ref` retains a warning that it need not always return the same pointer, but its integer-specific rematerialization branch is disabled by `if False` in this checkout (`pyobject.py:558–570`); active repeated integer materialization is **unverified**. Lucifer must independently preserve any observable identity it promises.

Borrowed references force retained boxes. Specialized two-int/two-float tuples cannot box a fresh temporary on every C lookup: cpyext constructs a pointer array owning references to the elements (`pypy/module/cpyext/tupleobject.py:19–28,70–95,153–163`). Lists switch to `CPyListStrategy`; storage allocates `PyObject*` entries with `make_ref`, and managed writes update the array and decref the previous entry (`pypy/module/cpyext/listobject.py:28–92`; `pypy/module/cpyext/sequence.py:368–389,505–517`). This is direct evidence that an unboxed strategy can lose its representation advantage at C exposure.

The ability to switch that list back to an ordinary object strategy (`pypy/module/cpyext/sequence.py:394–497`) is **not** evidence that Lucifer can do the same while unchanged CPython machine code retains naked `ob_item` access: PyPy's `PyList_GET_ITEM` is an API helper that obtains/converts storage (`pypy/module/cpyext/listobject.py:28–31,71–76`). On this **Python 3.12 branch**, the visible header is `{ob_refcnt, ob_type}` and `PyObject_HEAD_INIT` supplies those two fields. `ob_pypy_link` lives one word before the visible pointer in a 16-byte padded allocation prefix (`pypy/module/cpyext/parse/cpyext_object.h:6–19`; `pypy/module/cpyext/include/object.h:85–91`; `pyobject.py:25–70`). The standalone target **enables** `translation.rawrefcount_link_prefix = True`, although the generic RPython option defaults to false (`pypy/goal/targetpypystandalone.py:368–370`; `rpython/config/translationoption.py:272–281`). The prefix tag remains in the refcount after unlinking; exported static objects and prefix-less foreign objects instead use the separate mappings described above (`pypy/doc/discussion/rawrefcount.rst:59–128`; `pyobject.py:112–128,414–449`). This is concrete abi3 work, not proof of arbitrary non-limited 3.14 or free-threaded binary compatibility; the list/storage API differences remain.

Rawrefcount updates mappings after managed movement, identifies external native ownership as a managed root, and queues native deallocation outside collection so callbacks can execute and resurrect. The collector distinguishes permanent prefix tags from its legacy mode (`rpython/memory/gc/incminimark.py:3180–3213,3284–3403,3407–3476`; `pypy/doc/discussion/rawrefcount.rst:234–285`); the document's collection pseudocode still shows legacy tag subtraction, so it must not be applied verbatim to this build. The API wrapper also converts arguments/results/exceptions and manages GIL state (`pypy/module/cpyext/api.py:1098–1253`). Thus cpyext teaches a real dual-representation lifetime mechanism; it is neither a ready-made no-GIL bridge nor proof that stock full-API CPython wheels can use that representation.

### 5.2 Other implementations

| System | Mechanism and limit | Evidence |
| --- | --- | --- |
| GraalPy | Moving Java-managed objects have stable offheap native companions; native-created objects have managed wrappers. A large `MANAGED_REFCNT` represents managed ownership, with strong/weak wrapper transitions. Mixed cycles need native traversal plus managed graph integration. Custom pointer/macros are part of the mechanism. | [Implementation details](https://github.com/oracle/graalpython/blob/master/docs/contributor/IMPLEMENTATION_DETAILS.md). Its [native-extension guide](https://github.com/oracle/graalpython/blob/master/docs/user/Native-Extensions.md) expressly disclaims CPython binary ABI compatibility; packages need GraalPy builds and may receive pip patches. |
| HPy | Opaque owned handles, `HPyField`/`HPyGlobal` for stored references, explicit close; Universal ABI hides object layouts and permits moving GC. Hybrid mode permits legacy API but retains interpreter-ABI dependencies. Useful future interface, requires porting rather than accepting unchanged full-API NumPy. | [Overview / target ABIs](https://docs.hpyproject.org/en/latest/overview.html); [handle and field API](https://docs.hpyproject.org/en/latest/api.html). |
| Pyston | Original v1 replaced tracing GC with refcounting for compatible destruction/ownership. v2 full is a CPython fork, but explicitly API-compatible **not ABI-compatible**; lite runs inside CPython and accepts its existing packages. | [0.5 release](https://blog.pyston.org/2016/05/25/pyston-0-5-released/); [repository README](https://github.com/pyston/pyston/blob/pyston_main/README.md). |
| Cinder / CinderX | CPython-derived object/ownership model; JIT refcount insertion is late in SSA HIR. Borrowed values are promoted before their supporting location can change; destruction may run arbitrary code. Current CinderX is an extension of CPython, not evidence of a different native object ABI working without the GIL. | [Refcount insertion design](https://facebookincubator.github.io/cinderx/jit/internals/refcount-insertion); [README](https://github.com/facebookincubator/cinderx/blob/main/README.md). |
| Nogil / free-threaded CPython | Changes the concrete header/ABI and coordinates biased/deferred refcounting, immortals, object locks/critical sections, allocator reclamation, and GC. This preserves a direct native object interface without a universal proxy layer, but requires appropriately built extensions. | [PEP 703](https://peps.python.org/pep-0703/); pinned `Include/object.h`, `Include/refcount.h`, `Python/{brc,gc_free_threading}.c`. The implemented 3.14 headers, rather than the older PEP's draft struct, are the target. |

### 5.3 Published measurements, with their actual baselines

| Measurement | Reported result | What it establishes / does not establish |
| --- | --- | --- |
| PyPy cpyext, September 2018 | PyPy 5.8 microbenchmarks took **2.5–10× CPython 2.7 time**; integer-argument empty call cost about twice a no-argument call. PyPy 6.0 mostly beat CPython on the same microbenchmarks except integer/tuple allocation. | Boundary/conversion costs can dominate, and JIT effects can counteract them. Authors still warn of slow C-extension-heavy real programs. Not a current NumPy multiplier. [Primary report](https://pypy.org/posts/2018/09/inside-cpyext-why-emulating-cpython-c-8083064623681286567.html). |
| Pyston 0.5, May 2016 | About **10% slower than 0.4** after the refcounting change; NumPy test suite took about **2× CPython time**. | Whole-version/test-suite results, not isolated refcount or proxy taxes. NumPy 1.11 had one failure with an upstream-accepted test patch; modified Cython was needed. Not proof of today's unchanged-binary bar. [Release](https://blog.pyston.org/2016/05/25/pyston-0-5-released/). |
| Early HPy ultrajson, December 2019 | CPython ABI about parity with legacy `ujson`; Universal ABI about **10% slower on CPython**; HPy port **3× faster on PyPy**. | One early port, not universal HPy overhead. [Original sprint report](https://pypy.org/posts/2019/12/hpy-kick-off-sprint-report-1840829336092490938.html). |
| GraalVM Python HPy port, July 2022 | Matplotlib `basic.time_plot` HPy port nearly **4× faster than its GraalVM C-API port**, still behind CPython; fully ported Kiwisolver reached CPython parity. | CPython 3.8 / Matplotlib 3.4.x / Kiwisolver 1.3.2, i9-10885H; Matplotlib used Sulong bitcode. **Not current native-mode GraalPy proxy overhead.** [Authors' report](https://medium.com/graalvm/porting-matplotlib-from-c-api-to-hpy-aa32faa1f0b5). |
| Free-threading phase-II proposal, PEP 779 (2025) | About **10% linear slowdown** (macOS about 3%) and **15–20% more memory**, pyperformance geometric means. | Historical whole-runtime costs versus GIL builds; not isolated C-API cost or a Lucifer forecast. [PEP 779](https://peps.python.org/pep-0779/). |

A current controlled “cpyext costs X%,” current GraalPy native companion tax, and separate Cinder/Pyston object-model overhead are **unverified**. Do not combine the historical measurements into a predicted Lucifer slowdown. The pinned CPython `Doc/howto/free-threading-python.rst:104–156` still discusses 3.13 immortalization/~40% overhead while anticipating 3.14 improvements: that number must not be relabeled as a 3.14.0 measurement.

## 6. Comparing the three designs

These are design deductions from §§2–5, not measured Lucifer costs. “Canonical” means the C-visible storage is authoritative, not a disposable serialization.

| Concern | (a) Native CPython-compatible header and concrete layouts | (b) Managed objects plus C proxies | (c) Hybrid: concrete exposed types, flat private representations until exposure |
| --- | --- | --- | --- |
| Binary contract | Naturally matches selected ABI if **payloads and semantics**, not only header, match. All native-created objects are accepted directly. | Every proxy must itself be a genuine target-ABI object, including concrete payload/subclass layout when applicable. Rewritten access macros are unavailable for stock binaries. | Same requirement for every exposed object; the whitelist must expand to all accessed structures, builtin subclasses, extension types, and objects discoverable through their fields/callbacks. |
| Flat layout | Fixed 32-byte t header on allocated identity-bearing objects; custom flat numeric fields can follow it where their class/member contract permits. Existing builtin payload offsets cannot be repurposed. Virtual JIT values need no physical header until materialized. | Private bodies can be flat; pay companion allocation, mapping, retained buffers, conversion, and mixed-graph management. Mutable state has two locations unless the native one becomes authoritative. | Preserves private flat bodies until an identity/layout escape. Transition and alias redirection add complexity; after exposure the constrained portion behaves much like (a). |
| Storage strategies | A real canonical list's `ob_item` is always `PyObject**`; a scalar-specialized list cannot substitute `int64_t[]`. JIT-local virtual containers/typed kernels can still be specialized. | A list proxy needs retained boxes and a real pointer array. C writes must be visible to Python immediately, including callbacks; independent mirrors are unsound. | Unboxed strategy can exist before exposure. Materialization boxes/retains all visible elements, preserves identities/aliases, and makes pointer storage authoritative. Persistent C reachability prevents casually switching back. |
| Moving GC | Straightforward choice: nonmoving canonical heap. A separate private moving arena is possible only where no unknown native pointer can target it. | Nonmoving shells can point to movable hidden bodies, with maps/roots updated. Directly exposed inline data and extension-owned objects cannot move. Need a proven mixed tracing/refcount cycle algorithm. | Promote/pin before publishing any raw pointer. In-place header expansion generally cannot preserve address; materialization must occur before publication and update all managed aliases. Interior-pointer and mixed-cycle constraints remain. |
| Refcounts | Most direct implementation of native ownership/deallocator paths. JIT may eliminate balanced refcount traffic where lifetime/reentrancy remains correct. t ABI still needs owner-local/shared merge machinery and uniqueness APIs. | Biased counts can encode managed ownership, but inline increments do not invoke arbitrary proxy hooks. Must root native ownership without hidden leaks and reconcile observable uniqueness/deallocation. Copying PyPy/GraalPy's headers is not sufficient. | Canonical objects follow native ownership; virtual/flat objects require a carefully defined transition and accounting of live JIT references. Retaining one artificial reference can change resize/uniqueness behavior. |
| `__del__` / resources | Can reproduce prompt destruction where target ABI does, including delayed t-ABI cases. Refcount elimination must preserve the chosen observable behavior and reentrancy points. | Pure tracing typically delays finalization beyond C reference release; native-only zero-count destruction and Python finalizers need reconciliation. Reference cycles/resurrection make this more than a destructor callback. | Timing can change at exposure if managed-only and exposed objects use unrelated lifetime rules. Define one observable policy; do not let whether NumPy touched an object decide resource semantics accidentally. |
| Multicore | Reuse t-ABI locking/ownership rules; implement safe JIT accesses too. Header compatibility alone does not make container operations safe. | Need safe native ownership plus synchronized managed/native views and mapping operations; extra complexity under concurrent callbacks. | Exposure is a publication event requiring synchronization with JIT workers and deoptimization. Private-state assumptions must be invalidated before C can run. |

### 6.1 Why exposure is more than converting arguments and results

An extension can retain a strong reference in a global, follow a tuple/list array to a child without a runtime call, keep a byte/string/buffer interior pointer, call a type slot directly, or call Python and then resume native execution. A borrowed pointer can remain valid through a longer-lived owning container. **Call return is not proof that exposure ended.** Containers can transitively expose their elements; C-created instances may embed arbitrary owned native references. This follows from the pointer/slot definitions in §2 and the borrowed-reference ownership rules in `Doc/c-api/intro.rst`.

For (b)/(c), a sound default is monotonic conversion for a mutable object: once native storage can be retained, Python and C both operate on that storage until the object's lifetime ends. A more permissive reversal needs proof that no native observer, borrowed pointer, exported buffer, or interior pointer remains; no such proof mechanism was verified for Lucifer. CPython permits list backing arrays to move on resize, so that particular raw array pointer has a narrower validity interval; this does not permit a list to point to an unrelated unboxed representation during an otherwise legal C access (`cpython/listobject.h`; PyPy `cpyext/sequence.py:101–120`).

Converting an unboxed list costs at least a traversal and a pointer array, plus boxes/identity retention for values that do not already have canonical objects. It can propagate through nested exposed pointer fields. The cost is structural, **not quantified here**. Cached immutable companions are easier than mutable mirrors; dict/set mutation also has hash/equality callbacks and version/watch semantics (`Include/cpython/dictobject.h`; `Objects/dictobject.c`).

### 6.2 Refcounts, borrowed references, and finalization are distinct requirements

CPython explicitly says counts need not equal the number of references, especially for immortals, and `Py_REFCNT == 1` alone is insufficient for free-threaded exclusivity (`Doc/c-api/refcounting.rst:18–33`). Therefore exact numeric counts everywhere are not a necessary reading of compatibility. What is necessary is: owned references keep objects alive; borrowed references obey the source lifetime and concurrency contract; stealing APIs transfer ownership; zero/deallocator paths and weakrefs are sound; uniqueness answers never authorize unsafe mutation; and observable supported operations such as NumPy resize behave compatibly.

Free-threaded unique ownership checks owner identity, local count 1, and **the entire shared field equal to zero, flags included** (`Include/internal/pycore_object.h:181–195`). JIT aliases and borrowed stack values matter: `PyUnstable_Object_IsUniqueReferencedTemporary` inspects the current evaluation stack, so Lucifer needs an equivalent ownership answer for JIT frames rather than a stub returning the numeric header test (`Objects/object.c:2619–2641`). Cinder's late refcount insertion is relevant precedent for optimizing internal ownership while promoting borrowed values before invalidation (§5).

Ordinary DECREF at zero may immediately run arbitrary Python through deallocation/finalizers; `Py_CLEAR` deliberately clears the owning slot first (`Include/refcount.h:426–504`; `Doc/c-api/refcounting.rst`). Free-threaded CPython can queue cross-thread destruction for owner merging and defers selected object counts (`Objects/object.c:373–415`; `Python/brc.c:117–205`; `Include/internal/pycore_object.h:22–28`). Thus matching `cp314t` does **not** mean synchronously invoking every `__del__` after every Python last-reference drop. It also does not justify arbitrary tracing-GC delay or blanket immortalization: weakrefs, resurrection, resource release, and the NumPy behaviors above need an explicit compatibility policy and tests. No performance claim about a proposed policy is verified.

### 6.3 Which ABI can satisfy no GIL?

**Select `cp314t` first.** CPython itself requires separately built t extensions and supports neither Limited API nor stable ABI in that 3.14 build (`Doc/howto/free-threading-extensions.rst:394–416`; `Include/Python.h:48–52`). Ordinary `cp314` code expects `ob_type` at +8, while t code uses that area for flags/mutex/GC/local count and expects `ob_type` at +24. One raw pointer cannot present both structures simultaneously. Separate runtime variants or a much larger bridge would be required for both.

More fundamentally, ordinary full-API INCREF compiles non-atomic count updates; old list/dict borrowed-reference and extension-global assumptions rely on the GIL. Function shims cannot repair those instructions. CPython's free-threaded import fallback enables the GIL for extensions not declaring support (`Doc/howto/free-threading-extensions.rst:27–39`). That fallback does not meet Lucifer's fixed requirement. Serializing only extension entry is also not a general solution: concurrent JIT code may touch the same objects, and callbacks/releases/native TLS create further sharing. Process isolation or restrictions could support selected legacy workloads, but their different semantics and any general compatibility proof are **unverified**, and are not the recommendation.

The t ABI does not legalize every raw mutable access. List GET_ITEM and similar naked accesses require the appropriate protection; use owned-reference APIs where concurrent mutation can invalidate a borrow (`Doc/howto/free-threading-extensions.rst:103–116,151–195`). JIT-generated list/dict code must coordinate with **the same object locks and reclamation protocol** as C, not a private lock invisible to extensions. Critical sections can suspend outer locks when a thread detaches/blocks and resume the top section; a conventional recursive mutex is not equivalent (`Include/cpython/critical_section.h:19–58`).

M:N scheduling has a concrete ABI constraint: `_Py_ThreadId()` inlines OS-thread identity, including `%fs:0` on Linux x86-64, and compares it with `ob_tid` (`Include/object.h:182–256`). Green-process identity cannot replace that convention in existing binaries. **Design inference:** keep native call chains/callbacks on their OS worker; migration of managed work can use shared refcount paths while ownership remains with the original worker, with correct merging/worker teardown. Thread-state attach/detach, `PyGILState_*`, native TLS, pending critical sections, and extension-created OS threads need a defined integration. GraalPy similarly excludes native extensions on Java virtual threads because of native TLS ([embedding guidance](https://github.com/oracle/graalpython/blob/master/docs/user/Embedding-Native-Extensions.md)). This does not establish the details of Lucifer's scheduler implementation.

## 7. Recommendation and owner decisions

Use (a) as the first correctness substrate: **canonical `cp314t` header and concrete required payloads for materialized heap objects, nonmoving native-visible allocations, and full ownership/locking/GC integration**. The fixed concrete-type set includes more than tuple/list/bytes/str/float/int/type; Appendix A and the exposed structs in §2 define the audited surface. Preserve flat custom fields where no published layout conflicts. This reduces representation-coherence risk; it does not claim that mirroring CPython's complete runtime implementation is necessary or that the resulting performance is known.

Keep the owner's Python implementation requirement: builtin algorithms and methods can be Python compiled by Lucifer's JIT, with ABI slot thunks and native primitives for allocation, exact layout reads/writes, atomics/locks, reference ownership, GC integration, and C transitions. Choosing C-compatible storage does not dictate that `list.append`, string search, or numeric dispatch be implemented as C algorithms. Bootstrap, deallocator/finalizer reentrancy, and exported builtin type objects must still work before arbitrary Python execution is available (`README.md`; `Include/{objimpl,refcount}.h`; `Include/cpython/object.h`). The smallest adequate primitive surface has **not** been implemented or counted here.

Recover the Smalltalk representation gains first in JIT registers, virtual objects/containers, typed kernels, and escape analysis with identity-preserving deoptimization; materialize before a C-visible pointer can be published. These are compatible design directions, not measured speedups. Treat a moving private heap or broad hybrid (c) as a subsequent design requiring explicit exposure/lifetime proofs. Do not begin with whole-heap proxy duplication (b): PyPy demonstrates its machinery, but also conversion and borrowed-reference costs, and both PyPy/GraalPy change headers/ABI assumptions that Lucifer's unchanged-binary target cannot rely on (§5).

Decisions for the owner, without reopening the three fixed requirements:

| Decision | Recommended position / unresolved detail | Evidence driving it |
| --- | --- | --- |
| Meaning of “unmodified NumPy build” | Record acceptance against an upstream **CPython 3.14 free-threaded non-limited build**, built with ordinary upstream headers, with no NumPy source patches, Lucifer-specific headers, or fallback GIL. If ordinary `cp314` binaries are also required, record that as a separate unresolved compatibility obligation. | Distinct layouts and non-atomic ordinary inlines (§2, §6.3); upstream NumPy no-GIL declaration (§3). |
| Version/platform matrix | Pin exact CPython patch baseline, NumPy release/commit, compiler, OS/architecture, and build flags; initially use the audited x86-64 configuration. This report's NumPy checkout is a development snapshot. | Non-limited layouts/conditionals and digit/bitfield variations; §1. |
| Private API promise | Explicitly distinguish full supported non-limited/unstable APIs from `Py_BUILD_CORE` interpreter-private structures. Audit actual NumPy/generated binary dependencies before narrowing anything required by the public bar. | `Include/README.rst`; complete internal inventory in Appendix A. |
| Lifetime policy | Aim at `cp314t` ownership/finalization behavior, including its documented delayed cases; decide how JIT-local aliases and supported reference/uniqueness queries map to it. No arbitrary proxy bias or blanket immortality as a substitute for correctness. | NumPy resize/temporary/descriptor paths and CPython stack ownership (§3, §6.2). |
| GC and storage strategies | Nonmoving canonical heap initially; reserve unboxed strategies for proven private/virtual values. Decide whether later private moving storage justifies exposure, barrier, and mixed-cycle complexity. | Naked pointer stores/interior pointers (§2, §6.1). |
| Native scheduling contract | Native call chains stay on OS workers; specify thread-state/TLS attachment, callbacks, extension threads, blocked calls, and owner-queue teardown. | Inlined OS identity and critical-section rules (§6.3). |
| Unsupported/GIL-dependent extensions | Define explicit behavior for modules that do not declare free-threading support; silently enabling a GIL is excluded by the requirement. Declaration is an admission signal, not proof against extension bugs. | CPython import behavior and NumPy declaration (§3, §6.3). |

The next implementation review needs concrete evidence beyond this research: a pinned unmodified NumPy binary and its generated C/API/import inventory; layout probes against the chosen upstream headers; full NumPy results; and focused tests for retained/borrowed/interior references, C-created builtin subclasses, refcount-dependent resize, callbacks/resurrection, mixed cycles, concurrent container access, and scheduler attachment. These are **future validation requirements, not checks claimed to have passed**. No builds or benchmarks were performed for this report.

## 8. Validation of this research artifact

All local work was source reading and lightweight counting; vendor code was neither built, installed, nor executed. The Smalltalk VM was read only through the allowed `git show`/`ls-tree` forms. The embedded scanner was syntax-checked and rerun: it reproduced all **539 direct/layout definitions, 448 wrapper definitions, 104 nonzero NumPy rows**, and the stated file counts; every generated inventory identifier/line and NumPy count row was checked against the report. Markdown code fences and trailing whitespace were checked. Source counts retain inactive branches intentionally. Remaining unverified matters are active generated/binary dependencies, actual ABI conformance, NumPy pass/fail, the proposed GC/ownership/exposure implementation, and all Lucifer performance outcomes.

## Appendix A. Complete direct member/layout definition inventory

Prefix every header path below with `vendor/cpython/Include/`. Each entry is `identifier@line {members}`; the line locates the macro or inline definition, not every field access. `layout` denotes an explicitly included fieldless layout/initializer/payload operation. Conditional definitions and macro/inline pairs remain separate. Member lists are lexical, so e.g. `ob_base`, `long_value`, and `lv_tag` can describe one nested access. No core-only or mimalloc entry is thereby declared part of the ordinary extension contract.

| Header | Definitions |
| --- | --- |
| `cpython/abstract.h` | `PySequence_ITEM@86` {sq_item, tp_as_sequence}; `PySequence_Fast_ITEMS@101` {ob_item} |
| `cpython/bytearrayobject.h` | `PyByteArray_AS_STRING@20` {ob_start}; `PyByteArray_GET_SIZE@30` {ob_size} |
| `cpython/bytesobject.h` | `PyBytes_AS_STRING@23` {ob_sval} |
| `cpython/cellobject.h` | `PyCell_GET@24` {ob_ref}; `PyCell_SET@36` {ob_ref} |
| `cpython/classobject.h` | `PyMethod_GET_FUNCTION@34` {im_func}; `PyMethod_GET_SELF@39` {im_self}; `PyInstanceMethod_GET_FUNCTION@62` {func} |
| `cpython/code.h` | `PyCode_GetNumFree@166` {co_nfreevars}; `PyUnstable_Code_GetFirstFree@171` {co_nfreevars, co_nlocalsplus} |
| `cpython/compile.h` | `_PyCompilerFlags_INIT@32` {cf_feature_version, cf_flags} |
| `cpython/descrobject.h` | `PyDescr_TYPE@35` {d_type}; `PyDescr_NAME@36` {d_name} |
| `cpython/dictobject.h` | `PyDict_GET_SIZE@53` {ma_used} |
| `cpython/floatobject.h` | `PyFloat_AS_DOUBLE@15` {ob_fval} |
| `cpython/funcobject.h` | `PyFunction_GET_CODE@90` {func_code}; `PyFunction_GET_GLOBALS@95` {func_globals}; `PyFunction_GET_MODULE@100` {func_module}; `PyFunction_GET_DEFAULTS@105` {func_defaults}; `PyFunction_GET_KW_DEFAULTS@110` {func_kwdefaults}; `PyFunction_GET_CLOSURE@115` {func_closure}; `PyFunction_GET_ANNOTATIONS@120` {func_annotations} |
| `cpython/listobject.h` | `PyList_GET_SIZE@30` {ob_size}; `PyList_GET_ITEM@40` {ob_item}; `PyList_SET_ITEM@42` {allocated, ob_item} |
| `cpython/lock.h` | `_PyMutex_Lock@47` {_bits}; `_PyMutex_Unlock@58` {_bits}; `_PyMutex_IsLocked@69` {_bits} |
| `cpython/longintrepr.h` | `_PyLong_IsCompact@121` {long_value, lv_tag, ob_base, ob_type}; `_PyLong_CompactValue@129` {long_value, lv_tag, ob_base, ob_digit, ob_type} |
| `cpython/memoryobject.h` | `PyMemoryView_GET_BUFFER@41` {view}; `PyMemoryView_GET_BASE@47` {obj, view} |
| `cpython/methodobject.h` | `PyCFunction_GET_FUNCTION@40` {m_ml, ml_meth}; `PyCFunction_GET_SELF@45` {m_ml, m_self, ml_flags}; `PyCFunction_GET_FLAGS@54` {m_ml, ml_flags}; `PyCFunction_GET_CLASS@59` {m_ml, ml_flags, mm_class} |
| `cpython/monitoring.h` | `_PYMONITORING_IF_ACTIVE@117` {active} |
| `cpython/object.h` | `_Py_static_string_init@54` {index, string} |
| `cpython/objimpl.h` | `_PyObject_SIZE@5` {tp_basicsize}; `_PyObject_VAR_SIZE@23` {tp_basicsize, tp_itemsize} |
| `cpython/pylifecycle.h` | `_PyInterpreterConfig_INIT@52` {allow_daemon_threads, allow_exec, allow_fork, allow_threads, check_multi_interp_extensions, gil, use_main_obmalloc}; `_PyInterpreterConfig_LEGACY_INIT@72` {allow_daemon_threads, allow_exec, allow_fork, allow_threads, check_multi_interp_extensions, gil, use_main_obmalloc} |
| `cpython/pystats.h` | `_Py_INCREF_STAT_INC@185` {interpreter_increfs, object_stats}; `_Py_DECREF_STAT_INC@186` {interpreter_decrefs, object_stats}; `_Py_INCREF_IMMORTAL_STAT_INC@187` {interpreter_immortal_increfs, object_stats}; `_Py_DECREF_IMMORTAL_STAT_INC@188` {interpreter_immortal_decrefs, object_stats}; `_Py_INCREF_STAT_INC@190` {increfs, object_stats}; `_Py_DECREF_STAT_INC@191` {decrefs, object_stats}; `_Py_INCREF_IMMORTAL_STAT_INC@192` {immortal_increfs, object_stats}; `_Py_DECREF_IMMORTAL_STAT_INC@193` {immortal_decrefs, object_stats} |
| `cpython/setobject.h` | `PySet_GET_SIZE@64` {used} |
| `cpython/tupleobject.h` | `PyTuple_GET_ITEM@29` {ob_item}; `PyTuple_SET_ITEM@32` {ob_item} |
| `cpython/unicodeobject.h` | `PyUnicode_CHECK_INTERNED@209` {interned, state}; `PyUnicode_IS_ASCII@226` {ascii, state}; `PyUnicode_IS_COMPACT@233` {compact, state}; `PyUnicode_IS_COMPACT_ASCII@240` {ascii, state}; `PyUnicode_KIND@260` {kind, state}; `_PyUnicode_COMPACT_DATA@263` {layout}; `_PyUnicode_NONCOMPACT_DATA@270` {any, data}; `PyUnicode_GET_LENGTH@298` {length}; `PyUnicode_WRITE@308` {layout}; `PyUnicode_WRITE@326` {layout}; `PyUnicode_READ@332` {layout}; `PyUnicode_READ@345` {layout}; `_PyUnicodeWriter_Prepare@553` {maxchar, pos, size}; `_PyUnicodeWriter_PrepareKind@573` {kind} |
| `cpython/weakrefobject.h` | `PyWeakref_GET_OBJECT@51` {wr_object} |
| `datetime.h` | `_PyDateTime_HAS_TZINFO@120` {hastzinfo}; `PyDateTime_GET_YEAR@122` {data}; `PyDateTime_GET_MONTH@124` {data}; `PyDateTime_GET_DAY@125` {data}; `PyDateTime_DATE_GET_HOUR@127` {data}; `PyDateTime_DATE_GET_MINUTE@128` {data}; `PyDateTime_DATE_GET_SECOND@129` {data}; `PyDateTime_DATE_GET_MICROSECOND@130` {data}; `PyDateTime_DATE_GET_FOLD@134` {fold}; `PyDateTime_DATE_GET_TZINFO@135` {tzinfo}; `PyDateTime_TIME_GET_HOUR@139` {data}; `PyDateTime_TIME_GET_MINUTE@140` {data}; `PyDateTime_TIME_GET_SECOND@141` {data}; `PyDateTime_TIME_GET_MICROSECOND@142` {data}; `PyDateTime_TIME_GET_FOLD@146` {fold}; `PyDateTime_TIME_GET_TZINFO@147` {tzinfo}; `PyDateTime_DELTA_GET_DAYS@151` {days}; `PyDateTime_DELTA_GET_SECONDS@152` {seconds}; `PyDateTime_DELTA_GET_MICROSECONDS@153` {microseconds}; `PyDateTime_TimeZone_UTC@203` {TimeZone_UTC}; `PyDate_Check@206` {DateType}; `PyDate_CheckExact@207` {DateType}; `PyDateTime_Check@209` {DateTimeType}; `PyDateTime_CheckExact@210` {DateTimeType}; `PyTime_Check@212` {TimeType}; `PyTime_CheckExact@213` {TimeType}; `PyDelta_Check@215` {DeltaType}; `PyDelta_CheckExact@216` {DeltaType}; `PyTZInfo_Check@218` {TZInfoType}; `PyTZInfo_CheckExact@219` {TZInfoType}; `PyDate_FromDate@223` {DateType, Date_FromDate}; `PyDateTime_FromDateAndTime@226` {DateTimeType, DateTime_FromDateAndTime}; `PyDateTime_FromDateAndTimeAndFold@230` {DateTimeType, DateTime_FromDateAndTimeAndFold}; `PyTime_FromTime@234` {TimeType, Time_FromTime}; `PyTime_FromTimeAndFold@238` {TimeType, Time_FromTimeAndFold}; `PyDelta_FromDSU@242` {DeltaType, Delta_FromDelta}; `PyTimeZone_FromOffset@246` {TimeZone_FromTimeZone}; `PyTimeZone_FromOffsetAndName@249` {TimeZone_FromTimeZone}; `PyDateTime_FromTimestamp@253` {DateTimeType, DateTime_FromTimestamp}; `PyDate_FromTimestamp@257` {DateType, Date_FromTimestamp} |
| `internal/mimalloc/mimalloc/internal.h` | `mi_heap_is_backing@370` {heap_backing, tld}; `_mi_ptr_cookie@379` {cookie}; `_mi_heap_get_free_small_page@389` {pages_free_direct}; `mi_slice_to_page@405` {slice_count, slice_offset}; `mi_page_to_slice@410` {slice_count, slice_offset}; `_mi_page_segment@416` {slice_entries, slices}; `mi_slice_first@422` {slice_count, slice_offset, slices}; `_mi_segment_page_of@431` {slice_entries, slice_offset, slices}; `mi_page_block_size@455` {xblock_size}; `mi_page_is_huge@468` {kind}; `mi_segment_size@479` {segment_slices}; `mi_page_thread_free@488` {xthread_free}; `mi_page_thread_free_flag@492` {xthread_free}; `mi_page_heap@497` {xheap}; `mi_page_set_heap@501` {xheap}; `mi_page_all_free@525` {used}; `mi_page_has_any_available@531` {reserved, used}; `mi_page_immediate_available@537` {free}; `mi_page_mostly_used@543` {reserved, used}; `mi_page_queue@549` {pages}; `mi_page_is_in_full@558` {flags, in_full, x}; `mi_page_set_in_full@562` {flags, in_full, x}; `mi_page_has_aligned@566` {flags, has_aligned, x}; `mi_page_set_has_aligned@570` {flags, has_aligned, x}; `mi_block_nextx@633` {next}; `mi_block_set_nextx@646` {next}; `mi_block_next@657` {keys}; `mi_block_set_next@673` {keys}; `mi_commit_mask_create_empty@687` {mask}; `mi_commit_mask_create_full@693` {mask}; `mi_commit_mask_is_empty@699` {mask}; `mi_commit_mask_is_full@706` {mask}; `_mi_memid_create@731` {memkind}; `_mi_memid_create_os@742` {initially_committed, initially_zero, is_pinned} |
| `internal/mimalloc/mimalloc/types.h` | `mi_heap_stat_counter_increase@673` {stat, stats, tld}; `mi_heap_stat_increase@674` {stat, stats, tld}; `mi_heap_stat_decrease@675` {stat, stats, tld} |
| `internal/pycore_abstract.h` | `_PyIndex_Check@12` {nb_index, tp_as_number} |
| `internal/pycore_asdl.h` | `GENERATE_ASDL_SEQ_CONSTRUCTOR@52` {elements, size, typed_elements}; `asdl_seq_GET_UNTYPED@81` {elements}; `asdl_seq_GET@82` {typed_elements}; `asdl_seq_LEN@83` {size}; `asdl_seq_SET@86` {size, typed_elements}; `asdl_seq_SET@94` {typed_elements}; `asdl_seq_SET_UNTYPED@98` {elements, size}; `asdl_seq_SET_UNTYPED@106` {elements} |
| `internal/pycore_atexit.h` | `_PyAtExit_LockCallbacks@15` {ll_callbacks_lock}; `_PyAtExit_UnlockCallbacks@16` {ll_callbacks_lock} |
| `internal/pycore_backoff.h` | `is_unreachable_backoff_counter@38` {value_and_backoff}; `make_backoff_counter@44` {value_and_backoff}; `forge_backoff_counter@54` {value_and_backoff}; `restart_backoff_counter@62` {value_and_backoff}; `pause_backoff_counter@75` {value_and_backoff}; `advance_backoff_counter@83` {value_and_backoff}; `backoff_counter_triggers@91` {value_and_backoff} |
| `internal/pycore_blocks_output_buffer.h` | `_BlocksOutputBuffer_InitAndGrow@105` {allocated, list, max_length}; `_BlocksOutputBuffer_InitWithSize@153` {allocated, list, max_length}; `_BlocksOutputBuffer_Grow@191` {allocated, list, max_length}; `_BlocksOutputBuffer_GetDataSize@252` {allocated}; `_BlocksOutputBuffer_Finish@264` {allocated, list}; `_BlocksOutputBuffer_OnError@312` {list} |
| `internal/pycore_call.h` | `_PyVectorcall_FunctionInline@115` {tp_vectorcall_offset} |
| `internal/pycore_cell.h` | `PyCell_SwapTakeRef@18` {ob_ref}; `PyCell_GetRef@37` {ob_ref}; `_PyCell_GetStackRef@51` {ob_ref} |
| `internal/pycore_ceval.h` | `_PyEval_EvalFrame@114` {eval_frame, interp}; `_PyEval_IsGILEnabled@145` {ceval, enabled, gil, interp}; `_Py_MakeRecCheck@199` {c_stack_soft_limit}; `_Py_ReachedRecursionLimit@230` {c_stack_hard_limit, c_stack_soft_limit}; `_Py_set_eval_breaker_bit@323` {eval_breaker}; `_Py_unset_eval_breaker_bit@329` {eval_breaker}; `_Py_eval_breaker_bit_is_set@335` {eval_breaker} |
| `internal/pycore_ceval_state.h` | `_PyEval_RUNTIME_PERF_INIT@34` {extra_code_index, persist_after_fork, status} |
| `internal/pycore_code.h` | `_PyCode_CODE@16` {co_code_adaptive}; `_Py_OPCODE@21` {code, op}; `_Py_OPARG@22` {arg, op}; `_py_make_codeunit@24` {arg, code, op}; `_py_set_opcode@34` {code, op}; `_PyCode_HAS_EXECUTORS@46` {co_executors}; `_PyCode_HAS_INSTRUMENTATION@48` {_co_instrumentation_version}; `_PyCode_GetTLBCArray@526` {co_tlbc}; `_PyCode_GetTLBCFast@535` {entries, size, tlbc_index} |
| `internal/pycore_critical_section.h` | `_Py_CRITICAL_SECTION_ASSERT_OBJECT_LOCKED@55` {ob_mutex}; `_PyCriticalSection_BeginMutex@97` {_cs_mutex, _cs_prev, critical_section}; `_PyCriticalSection_Begin@112` {ob_mutex}; `_PyCriticalSection_Pop@122` {_cs_prev, critical_section}; `_PyCriticalSection_End@134` {_cs_mutex}; `_PyCriticalSection2_BeginMutex@148` {_cs_base, _cs_mutex, _cs_mutex2, _cs_prev, critical_section}; `_PyCriticalSection2_Begin@188` {ob_mutex}; `_PyCriticalSection2_End@195` {_cs_base, _cs_mutex, _cs_mutex2}; `_PyCriticalSection_AssertHeld@214` {_cs_base, _cs_mutex, _cs_mutex2, critical_section} |
| `internal/pycore_crossinterp.h` | `_PyXIData_DATA@90` {data}; `_PyXIData_OBJ@91` {obj}; `_PyXIData_INTERPID@92` {interpid}; `_PyXIData_SET_FREE@111` {free}; `_PyXIData_CHECK_FREE@115` {free}; `_PyXIData_SET_NEW_OBJECT@124` {new_object}; `_PyXIData_CHECK_NEW_OBJECT@128` {new_object}; `_PyXI_GET_GLOBAL_STATE@270` {runtime, xi}; `_PyXI_GET_STATE@271` {xi} |
| `internal/pycore_debug_offsets.h` | `_Py_Debug_interpreter_state_tlbc_generation@57` {tlbc_generation}; `_Py_DebugOffsets_INIT@239` {_base, argcount, asciiobject_size, builtins, bytes_object, ceval_gil, co_code_adaptive, co_tlbc, code_object, code_object_generation, collecting, cookie, current_frame, datastack_chunk, debugger_pending_call, debugger_script_path, debugger_script_path_size, debugger_support, dict_object, eval_breaker, executable, filename, finalizing, firstlineno, float_object, free_threaded, gc, gen_object, gi_frame_state, gi_iframe, gi_name, gil, gil_runtime_state, gil_runtime_state_enabled, gil_runtime_state_holder, gil_runtime_state_locked, head, id, imports_modules, instr_ptr, interp, interpreter_frame, interpreter_state, interpreters_head, last_holder, length, linetable, list_object, llist_node, localsplus, localspluskinds, localsplusnames, locked, long_object, lv_tag, ma_keys, ma_values, main, mask, modules, name, native_thread_id, next, ob_digit, ob_fval, ob_item, ob_size, ob_sval, ob_type, owner, prev, previous, pyobject, qualname, remote_debug, remote_debugger_support, remote_debugging_enabled, runtime_state, set_object, size, stackpointer, state, status, sysdict, table, thread_id, thread_state, threads_head, threads_main, tlbc_generation, tlbc_index, tp_flags, tp_name, tp_repr, tuple_object, type_object, unicode_object, used, version} |
| `internal/pycore_dict.h` | `_PyDict_HasSplitTable@50` {ma_values}; `DK_LOG_SIZE@235` {dk_log2_size}; `_DK_ENTRIES@242` {dk_indices, dk_log2_index_bytes}; `DK_ENTRIES@248` {dk_kind}; `DK_UNICODE_ENTRIES@252` {dk_kind}; `DK_IS_UNICODE@257` {dk_kind}; `_PyDict_NotifyEvent@273` {_ma_watcher_tag}; `get_insertion_order_array@295` {capacity, values}; `_PyDictValues_AddToInsertionOrder@301` {capacity, size}; `shared_keys_usable_size@313` {dk_nentries, dk_usable}; `_PyInlineValuesSize@328` {ht_cached_keys}; `_PyDict_UniqueId@354` {_ma_watcher_tag} |
| `internal/pycore_dict_state.h` | `_dict_state_INIT@19` {next_keys_version} |
| `internal/pycore_dtoa.h` | `_dtoa_state_INIT@21` {dtoa, preallocated, preallocated_next} |
| `internal/pycore_faulthandler.h` | `_faulthandler_runtime_state_INIT@89` {fatal_error, fd} |
| `internal/pycore_freelist.h` | `_Py_freelists_GET@17` {freelists, interp, object_state}; `_Py_FREELIST_FREE@33` {NAME}; `_Py_FREELIST_PUSH@37` {NAME}; `_Py_FREELIST_POP@41` {NAME}; `_Py_FREELIST_POP_MEM@46` {NAME}; `_Py_FREELIST_SIZE@49` {NAME, size}; `_PyFreeList_Push@51` {freelist, size}; `_PyFreeList_PopNoStats@73` {freelist, size} |
| `internal/pycore_function.h` | `_PyFunction_GET_BUILTINS@44` {func_builtins} |
| `internal/pycore_gc.h` | `_Py_AS_GC@17` {layout}; `_Py_FROM_GC@23` {layout}; `_PyObject_SET_GC_BITS@50` {ob_gc_bits}; `_PyObject_HAS_GC_BITS@57` {ob_gc_bits}; `_PyObject_CLEAR_GC_BITS@63` {ob_gc_bits}; `_PyObject_GC_IS_TRACKED@73` {_gc_next}; `_PyGCHead_NEXT@161` {_gc_next}; `_PyGCHead_SET_NEXT@165` {_gc_next}; `_PyGCHead_PREV@172` {_gc_prev}; `_PyGCHead_SET_PREV@177` {_gc_prev}; `_PyGC_FINALIZED@183` {_gc_prev}; `_PyGC_SET_FINALIZED@191` {_gc_prev}; `_PyGC_CLEAR_FINALIZED@199` {_gc_prev}; `_PyObject_GC_TRACK@222` {_gc_next, _gc_prev, gc, head, visited_space, young}; `_PyObject_GC_UNTRACK@262` {_gc_next, _gc_prev} |
| `internal/pycore_genobject.h` | `_PyGen_GetGeneratorFromFrame@16` {owner} |
| `internal/pycore_global_objects.h` | `_Py_GLOBAL_OBJECT@15` {NAME, static_objects}; `_Py_SINGLETON@17` {NAME}; `_Py_INTERP_CACHED_OBJECT@21` {NAME, cached_objects}; `_Py_INTERP_STATIC_OBJECT@25` {NAME, static_objects}; `_Py_INTERP_SINGLETON@27` {NAME} |
| `internal/pycore_global_objects_fini_generated.h` | `_PyStaticObjects_CheckRefcnt@23` {ascii, latin1} |
| `internal/pycore_global_strings.h` | `_Py_ID@830` {_ascii, _py_, identifiers, ob_base}; `_Py_STR@832` {_ascii, _py_, literals, ob_base}; `_Py_LATIN1_CHR@834` {ascii, latin1} |
| `internal/pycore_hashtable.h` | `_Py_SLIST_ITEM_NEXT@21` {next}; `_Py_SLIST_HEAD@23` {head}; `_Py_hashtable_get_entry@122` {get_entry_func} |
| `internal/pycore_import.h` | `DLOPENFLAGS_INIT@41` {dlopenflags}; `IMPORTS_INIT@47` {find_and_load, header} |
| `internal/pycore_initconfig.h` | `_PyStatus_OK@23` {_type}; `_PyStatus_ERR@26` {_type, err_msg, func}; `_PyStatus_EXIT@34` {_type, exitcode}; `_PyStatus_IS_ERROR@38` {_type}; `_PyStatus_IS_EXIT@40` {_type}; `_PyStatus_EXCEPTION@42` {_type}; `_PyStatus_UPDATE_FUNC@44` {func}; `_PyWideStringList_INIT@53` {items, length}; `_PyPreCmdline_INIT@110` {dev_mode, isolated, use_environment} |
| `internal/pycore_instruction_sequence.h` | `SAME_JUMP_TARGET_LABEL@59` {id} |
| `internal/pycore_interp.h` | `_PyInterpreterState_GetFinalizing@30` {_finalizing}; `_PyInterpreterState_GetFinalizingID@35` {_finalizing_id}; `_PyInterpreterState_SetFinalizing@40` {_finalizing, _finalizing_id, thread_id} |
| `internal/pycore_interp_structs.h` | `_PyGC_Head_UNUSED@169` {layout} |
| `internal/pycore_interpframe.h` | `_PyInterpreterFrame_LASTI@17` {instr_ptr}; `_PyFrame_GetCode@20` {f_executable}; `_PyFrame_GetBytecode@27` {entries, size, tlbc_index}; `_PyFrame_GetFunction@40` {f_funcobj}; `_PyFrame_Stackbase@46` {co_nlocalsplus, localsplus}; `_PyFrame_StackPeek@50` {co_nlocalsplus, localsplus, stackpointer}; `_PyFrame_StackPop@56` {co_nlocalsplus, localsplus, stackpointer}; `_PyFrame_StackPush@62` {stackpointer}; `_PyFrame_NumSlotsForCodeObject@69` {co_framesize}; `_PyFrame_Copy@78` {f_builtins, f_executable, f_funcobj, f_globals, f_locals, frame_obj, instr_ptr, localsplus, previous, stackpointer, tlbc_index}; `_PyFrame_InitializeTLBC@103` {instr_ptr, tlbc_index}; `_PyFrame_Initialize@126` {co_nlocalsplus, f_builtins, f_executable, f_funcobj, f_globals, f_locals, frame_obj, func_builtins, func_globals, instr_ptr, lltrace, localsplus, owner, previous, return_offset, stackpointer, visited}; `_PyFrame_GetLocalsArray@161` {localsplus}; `_PyFrame_GetStackPointer@170` {stackpointer}; `_PyFrame_SetStackPointer@181` {stackpointer}; `_PyFrame_IsIncomplete@199` {_co_firsttraceable, instr_ptr, owner}; `_PyFrame_GetFirstComplete@210` {previous}; `_PyThreadState_GetFrame@219` {current_frame}; `_PyFrame_GetFrameObject@233` {frame_obj}; `_PyThreadState_HasStackSpace@269` {datastack_limit, datastack_top}; `_PyFrame_PushUnchecked@289` {co_framesize, datastack_limit, datastack_top, func_code}; `_PyFrame_PushTrampolineUnchecked@305` {co_framesize, co_nlocalsplus, co_stacksize, datastack_limit, datastack_top, f_builtins, f_executable, f_funcobj, f_globals, f_locals, frame_obj, instr_ptr, lltrace, localsplus, owner, previous, return_offset, stackpointer, visited} |
| `internal/pycore_list.h` | `_PyList_ITEMS@28` {ob_item}; `_PyList_AppendTakeRef@34` {allocated, ob_item} |
| `internal/pycore_llist.h` | `llist_for_each@43` {next}; `llist_for_each_safe@47` {next}; `llist_init@53` {next, prev}; `llist_empty@61` {next}; `llist_insert_tail@68` {next, prev}; `llist_remove@78` {next, prev}; `llist_concat@90` {next, prev} |
| `internal/pycore_lock.h` | `PyMutex_LockFast@20` {_bits}; `PyMutex_LockFlags@52` {_bits}; `_PyRawMutex_Lock@105` {v}; `_PyRawMutex_Unlock@115` {v}; `_PyOnceFlag_CallOnce@139` {v} |
| `internal/pycore_long.h` | `_PyLong_IsNonNegativeCompact@203` {long_value, lv_tag}; `_PyLong_BothAreCompact@210` {long_value, lv_tag}; `_PyLong_IsZero@217` {long_value, lv_tag}; `_PyLong_IsNegative@223` {long_value, lv_tag}; `_PyLong_IsPositive@229` {long_value, lv_tag}; `_PyLong_DigitCount@235` {long_value, lv_tag}; `_PyLong_SignedDigitCount@243` {long_value, lv_tag}; `_PyLong_CompactSign@251` {long_value, lv_tag}; `_PyLong_NonCompactSign@259` {long_value, lv_tag}; `_PyLong_SameSign@268` {long_value, lv_tag}; `_PyLong_SetSignAndDigitCount@277` {long_value, lv_tag}; `_PyLong_SetDigitCount@286` {long_value, lv_tag}; `_PyLong_FlipSign@295` {long_value, lv_tag}; `_PyLong_DIGIT_INIT@302` {long_value, lv_tag, ob_base} |
| `internal/pycore_moduleobject.h` | `_PyModule_GetDef@32` {md_def}; `_PyModule_GetState@37` {md_state}; `_PyModule_GetDict@42` {md_dict} |
| `internal/pycore_object.h` | `_PyObject_HEAD_INIT@75` {ob_flags, ob_gc_bits, ob_ref_local, ob_type}; `_PyObject_HEAD_INIT@84` {ob_flags, ob_refcnt, ob_type}; `_PyObject_HEAD_INIT@91` {ob_refcnt, ob_type}; `_PyVarObject_HEAD_INIT@98` {ob_base, ob_size}; `_PyReftracerTrack@111` {ref_tracer, tracer_data, tracer_func}; `_Py_DEC_REFTOTAL@129` {object_state, reftotal}; `_Py_RefcntAdd@134` {ob_ref_local, ob_ref_shared, ob_refcnt}; `_PyObject_IsUniquelyReferenced@183` {ob_ref_local, ob_ref_shared}; `_Py_SetMortal@203` {ob_ref_local, ob_ref_shared, ob_refcnt, ob_tid}; `_Py_DECREF_SPECIALIZED@232` {ob_refcnt}; `_Py_DECREF_NO_DEALLOC@255` {ob_refcnt}; `_PyType_HasFeature@314` {tp_flags}; `_Py_THREAD_INCREF_OBJECT@345` {refcounts, size, values}; `_Py_INCREF_TYPE@367` {unique_id}; `_Py_INCREF_CODE@389` {_co_unique_id}; `_Py_THREAD_DECREF_OBJECT@395` {refcounts, size, values}; `_Py_DECREF_TYPE@417` {unique_id}; `_Py_DECREF_CODE@429` {_co_unique_id}; `Py_DECREF_MORTAL@439` {ob_refcnt}; `_Py_DECREF_MORTAL_SPECIALIZED@455` {ob_refcnt}; `Py_DECREF_MORTAL@477` {ob_refcnt}; `Py_DECREF_MORTAL_SPECIALIZED@487` {ob_refcnt}; `_Py_TryIncrefFast@549` {ob_ref_local}; `_Py_TryIncRefShared@569` {ob_ref_shared}; `_Py_NewRefWithLock@644` {ob_ref_shared}; `_PyObject_SetMaybeWeakref@678` {ob_ref_shared}; `_PyObject_ResurrectStart@702` {ob_ref_local, ob_ref_shared, ob_tid}; `_PyObject_ResurrectEnd@724` {ob_ref_local, ob_ref_shared}; `_PyObject_GET_WEAKREFS_LISTPTR@792` {tp_flags, tp_weaklistoffset}; `_PyObject_GET_WEAKREFS_LISTPTR_FROM_OFFSET@819` {tp_flags, tp_weaklistoffset}; `_PyObject_IS_GC@832` {tp_is_gc}; `_PyObject_HashFast@841` {hash}; `_PyType_SUPPORTS_WEAKREFS@874` {tp_weaklistoffset}; `_PyObject_ManagedDictPointer@924` {tp_flags}; `_PyObject_GetManagedDict@931` {dict}; `_PyObject_InlineValues@938` {tp_basicsize, tp_flags}; `_Py_INCREF_MORTAL@1013` {ob_refcnt} |
| `internal/pycore_object_alloc.h` | `_PyObject_GetAllocationHeap@17` {heaps, mimalloc}; `_PyObject_MallocWithType@38` {current_object_heap, heaps, mimalloc}; `_PyObject_ReallocWithType@53` {current_object_heap, heaps, mimalloc} |
| `internal/pycore_object_stack.h` | `_PyObjectStack_Push@36` {head, n, objs, prev}; `_PyObjectStack_Pop@57` {head, n, objs, prev}; `_PyObjectStack_Size@74` {head, n, prev} |
| `internal/pycore_obmalloc_init.h` | `PTA@15` {used}; `_obmalloc_global_state_INIT@57` {dump_debug_stats} |
| `internal/pycore_opcode_metadata.h` | `IS_VALID_OPCODE@1023` {valid_entry}; `OPCODE_HAS_ARG@1043` {flags}; `OPCODE_HAS_CONST@1044` {flags}; `OPCODE_HAS_NAME@1045` {flags}; `OPCODE_HAS_JUMP@1046` {flags}; `OPCODE_HAS_FREE@1047` {flags}; `OPCODE_HAS_LOCAL@1048` {flags}; `OPCODE_HAS_EVAL_BREAK@1049` {flags}; `OPCODE_HAS_DEOPT@1050` {flags}; `OPCODE_HAS_ERROR@1051` {flags}; `OPCODE_HAS_ESCAPES@1052` {flags}; `OPCODE_HAS_EXIT@1053` {flags}; `OPCODE_HAS_PURE@1054` {flags}; `OPCODE_HAS_PASSTHROUGH@1055` {flags}; `OPCODE_HAS_OPARG_AND_1@1056` {flags}; `OPCODE_HAS_ERROR_NO_POP@1057` {flags}; `OPCODE_HAS_NO_SAVE_IP@1058` {flags}; `is_pseudo_target@2102` {targets} |
| `internal/pycore_optimizer.h` | `uop_get_target@135` {format, target}; `uop_get_jump_target@141` {format, jump_target}; `uop_get_error_target@147` {error_target, format}; `is_terminator@301` {opcode} |
| `internal/pycore_parser.h` | `_parser_runtime_state_INIT@18` {Name, col_offset, ctx, dummy_name, end_col_offset, end_lineno, id, kind, lineno, mutex, v}; `_parser_runtime_state_INIT@32` {Name, col_offset, ctx, dummy_name, end_col_offset, end_lineno, id, kind, lineno, v} |
| `internal/pycore_pyerrors.h` | `_PyErr_Occurred@74` {current_exception}; `_PyErr_ClearExcState@83` {exc_value} |
| `internal/pycore_pyhash.h` | `_py_urandom_cache_INIT@75` {fd}; `pyhash_state_INIT@83` {urandom_cache} |
| `internal/pycore_pymem_init.h` | `PYDBGRAW_ALLOC@56` {allocators, debug, raw}; `PYDBGMEM_ALLOC@58` {allocators, debug, mem}; `PYDBGOBJ_ALLOC@60` {allocators, debug, obj}; `_Py_mem_free_queue_INIT@95` {head} |
| `internal/pycore_pystate.h` | `_PyThreadState_IsAttached@124` {state}; `_PyInterpreterState_GET@206` {interp}; `HEAD_LOCK@260` {interpreters, mutex}; `HEAD_UNLOCK@262` {interpreters, mutex}; `_Py_FOR_EACH_TSTATE_UNLOCKED@265` {head, next, threads}; `_Py_FOR_EACH_TSTATE_BEGIN@267` {runtime}; `_Py_FOR_EACH_TSTATE_END@270` {runtime}; `_Py_RecursionLimit_GetMargin@323` {c_stack_hard_limit, c_stack_soft_limit} |
| `internal/pycore_pythread.h` | `_pythread_RUNTIME_INIT@85` {handles} |
| `internal/pycore_qsbr.h` | `_Py_qsbr_shared_current@95` {wr_seq}; `_Py_qsbr_quiescent_state@103` {seq, shared}; `_Py_qbsr_goal_reached@112` {rd_seq, shared}; `_Py_qsbr_should_process@131` {should_process} |
| `internal/pycore_runtime.h` | `_PyRuntimeState_GetFinalizing@35` {_finalizing}; `_PyRuntimeState_GetFinalizingID@40` {_finalizing_id}; `_PyRuntimeState_SetFinalizing@45` {_finalizing, _finalizing_id, thread_id} |
| `internal/pycore_runtime_init.h` | `_PyRuntimeState_INIT@39` {_main_interpreter, allocators, ascii, autoTSSkey, bytes_characters, bytes_empty, ceval, check_enabled, context_token_missing, data_lookup, debug, debug_offsets, double_format, faulthandler, fileutils, float_format, float_state, force_ascii, gilstate, global, hamt_bitmap_node_empty, identifiers, interpreters, is_debug_enabled, is_global, latin1, literals, max, maxloop, next_id, next_version_tag, ob_base, ob_hash, obj_arena, obmalloc, parser, pending_mainthread, perf, pyhash_state, ref_tracer, registry, signals, singletons, small_ints, standard, static_objects, stoptheworld, strings, threads, tracemalloc, tracer_data, tracer_func, tuple_empty, types, xi}; `_PyInterpreterState_INIT@123` {_initial_thread, _not_used, _whence, args, ceval, dict_state, dtoa, enabled, func_state, gc, h_root, hamt_empty, id_refcount, imports, last_resort_memory_error, max, maxloop, mem_free_queue, next_version, next_version_tag, ob_base, old, pending, phase, preallocated, qsbr, rd_seq, recursion_limit, singletons, static_objects, threads, threshold, types, work_to_do, wr_seq, young}; `_PyThreadStateImpl_INIT@177` {base, refcount}; `_PyThreadState_INIT@184` {_whence, context_ver, py_recursion_limit}; `_PyBytes_SIMPLE_INIT@194` {ob_shash, ob_sval}; `_PyUnicode_ASCII_BASE_INIT@205` {ascii, compact, hash, kind, length, ob_base, state, statically_allocated}; `_PyASCIIObject_INIT@217` {_ascii, _data}; `INIT_STR@222` {_py_}; `INIT_ID@224` {_py_}; `_PyUnicode_LATIN1_INIT@226` {_base, _data, _latin1, utf8, utf8_length} |
| `internal/pycore_signal.h` | `_signals_WAKEUP_INIT@83` {fd, use_send, warn_on_full_buffer}; `_signals_WAKEUP_INIT@86` {fd, warn_on_full_buffer}; `_signals_RUNTIME_INIT@90` {wakeup} |
| `internal/pycore_stackref.h` | `PyStackRef_None@68` {index}; `PyStackRef_False@69` {index}; `PyStackRef_True@70` {index}; `PyStackRef_IsNull@74` {index}; `_PyStackRef_AsPyObjectBorrow@98` {index}; `PyStackRef_IsTaggedInt@138` {index}; `PyStackRef_IsTaggedInt@244` {bits}; `PyStackRef_TagInt@250` {bits}; `PyStackRef_UntagInt@257` {bits}; `PyStackRef_IsNull@275` {bits}; `PyStackRef_True@276` {bits}; `PyStackRef_False@277` {bits}; `PyStackRef_None@278` {bits}; `PyStackRef_AsPyObjectBorrow@287` {bits}; `PyStackRef_IsDeferred@294` {bits}; `PyStackRef_NotDeferred_AsPyObject@296` {bits}; `_PyStackRef_FromPyObjectSteal@313` {bits}; `PyStackRef_MakeHeapSafe@333` {bits}; `PyStackRef_FromPyObjectStealMortal@343` {bits}; `PyStackRef_FromPyObjectNew@353` {bits}; `PyStackRef_FromPyObjectImmortal@368` {bits}; `PyStackRef_Borrow@406` {bits}; `BITS_TO_PTR@451` {bits}; `BITS_TO_PTR_MASKED@452` {bits}; `PyStackRef_IsNull@457` {bits}; `PyStackRef_True@458` {bits}; `PyStackRef_False@459` {bits}; `PyStackRef_None@460` {bits}; `PyStackRef_IsTrue@462` {bits}; `PyStackRef_IsFalse@463` {bits}; `PyStackRef_IsNone@464` {bits}; `PyStackRef_CheckValid@468` {bits}; `PyStackRef_RefcountOnObject@491` {bits}; `PyStackRef_Borrow@493` {bits}; `PyStackRef_RefcountOnObject@496` {bits}; `PyStackRef_Borrow@509` {bits}; `PyStackRef_FromPyObjectSteal@527` {bits, ob_flags}; `PyStackRef_FromPyObjectStealMortal@541` {bits}; `_PyStackRef_FromPyObjectNew@551` {bits}; `_PyStackRef_FromPyObjectNewMortal@565` {bits}; `PyStackRef_FromPyObjectImmortal@577` {bits}; `PyStackRef_IsHeapSafe@600` {bits}; `PyStackRef_MakeHeapSafe@606` {bits}; `PyStackRef_XCLOSE@654` {bits}; `PyStackRef_Is@678` {bits}; `_PyThreadState_PushCStackRef@727` {c_stack_refs, next, ref}; `_PyThreadState_PopCStackRef@738` {c_stack_refs, next, ref}; `_Py_TryIncrefCompareStackRef@751` {bits} |
| `internal/pycore_stats.h` | `STAT_INC@18` {name, opcode_stats, specialization}; `STAT_DEC@19` {name, opcode_stats, specialization}; `OPCODE_EXE_INC@20` {execution_count, opcode_stats}; `CALL_STAT_INC@21` {call_stats, name}; `OBJECT_STAT_INC@22` {name, object_stats}; `OBJECT_STAT_INC_COND@23` {name, object_stats}; `EVAL_CALL_STAT_INC@25` {call_stats, eval_calls}; `EVAL_CALL_STAT_INC_IF_FUNCTION@26` {call_stats, eval_calls}; `GC_STAT_ADD@28` {gc_stats, name}; `OPT_STAT_INC@29` {name, optimization_stats}; `OPT_STAT_ADD@30` {name, optimization_stats}; `UOP_STAT_INC@31` {name, opcode, optimization_stats}; `UOP_PAIR_INC@32` {opcode, optimization_stats, pair_count}; `OPT_UNSUPPORTED_OPCODE@39` {optimization_stats, unsupported_opcode}; `OPT_ERROR_IN_OPCODE@40` {error_in_opcode, optimization_stats}; `OPT_HIST@41` {name, optimization_stats}; `RARE_EVENT_STAT_INC@49` {name, rare_event_stats}; `OPCODE_DEFERRED_INC@50` {deferred, opcode_stats, specialization}; `RARE_EVENT_INTERP_INC@77` {name, rare_events} |
| `internal/pycore_symtable.h` | `SRC_LOCATION_FROM_AST@53` {col_offset, end_col_offset, end_lineno, lineno} |
| `internal/pycore_tracemalloc.h` | `_tracemalloc_runtime_state_INIT@103` {config, initialized, max_nframe, reentrant_key, tracing} |
| `internal/pycore_tuple.h` | `_PyTuple_ITEMS@24` {ob_item}; `_PyTuple_RESET_HASH_CACHE@36` {ob_hash} |
| `internal/pycore_typeobject.h` | `_PyStaticType_GET_WEAKREFS_LISTPTR@44` {tp_weaklist}; `_PyType_GetModuleState@75` {ht_module, md_state, tp_flags} |
| `internal/pycore_weakref.h` | `WEAKREF_LIST_LOCK@18` {weakref_locks}; `LOCK_WEAKREFS_FOR_WR@28` {weakrefs_lock}; `UNLOCK_WEAKREFS_FOR_WR@30` {weakrefs_lock}; `_is_dead@56` {ob_ref_shared}; `_PyWeakref_GET_REF@72` {wr_object}; `_PyWeakref_IS_DEAD@99` {wr_object} |
| `object.h` | `PyObject_HEAD@60` {layout}; `PyObject_HEAD_INIT@71` {layout}; `PyObject_HEAD_INIT@82` {layout}; `PyVarObject_HEAD_INIT@89` {layout}; `PyObject_VAR_HEAD@101` {layout}; `_Py_IsOwnedByCurrentThread@250` {ob_tid}; `_Py_TYPE@268` {ob_type}; `Py_SIZE@283` {ob_size}; `Py_SET_TYPE@300` {ob_type}; `Py_SET_SIZE@307` {ob_size}; `PyType_HasFeature@776` {tp_flags} |
| `pymacro.h` | `Py_MEMBER_SIZE@85` {member} |
| `refcount.h` | `_Py_REFCNT@105` {ob_ref_local, ob_ref_shared, ob_refcnt}; `_Py_IsImmortal@123` {ob_ref_local, ob_refcnt}; `_Py_IsStaticImmortal@137` {ob_flags, ob_refcnt}; `Py_SET_REFCNT@150` {ob_ref_local, ob_ref_shared, ob_refcnt, ob_tid}; `Py_INCREF@250` {ob_ref_local, ob_ref_shared, ob_refcnt}; `Py_DECREF@335` {ob_ref_local}; `Py_DECREF@361` {ob_ref_local}; `Py_DECREF@384` {ob_refcnt, ob_refcnt_full}; `Py_DECREF@409` {ob_refcnt} |

The bundled allocator also contains **implicit inline C++ class methods**, outside the C macro/static-inline census. In `vendor/cpython/Include/internal/mimalloc/mimalloc.h`, `_mi_heap_stl_allocator_common` initializes `heap` at **497**, reads it in `allocate` at **500/503**, reads it in `collect` at **510**, compares it in `is_equal` at **511**, resets it in the default constructor at **517–519**, and copies it in constructors at **521/522**: eight explicitly defined methods/variants touching allocator state. `allocate@501`, derived constructors **532–534/549–551**, copy-selection methods **536/553**, and equality operators **541–542/558–559** reach these operations or copy that state indirectly. Destructor invocations **447/450** operate on client objects rather than CPython fields. These are allocator C++ conveniences, not additional Python object layouts; listing them closes the implicit-inline exception rather than silently excluding bundled headers.

## Appendix B. Complete transitive wrapper/alias inventory

These definitions contain no explicit member access and were not explicit layout seeds, but mention a layout-sensitive identifier transitively. This includes assertion/type-check casts, refcount wrappers, struct-declaration macros, and conditional no-op alternatives with the same name. **Lexical reachability overapproximates actual expansion**: no platform/version branches are selected; merely mentioning a function with an internal inline variant does not mean its public out-of-line call bakes that body into a binary. Direct definitions are in Appendix A; together the tables retain all selected definitions.

### B.1 Extension-facing graph (top-level + cpython headers only)

220 definitions; prefix paths with `vendor/cpython/Include/`.

| Header | Wrapper / alias definitions |
| --- | --- |
| `boolobject.h` | `PyBool_Check@12`; `Py_RETURN_TRUE@41`; `Py_RETURN_FALSE@42`; `Py_RETURN_TRUE@44`; `Py_RETURN_FALSE@45` |
| `bytearrayobject.h` | `PyByteArray_Check@24`; `PyByteArray_CheckExact@25` |
| `bytesobject.h` | `PyBytes_Check@27`; `PyBytes_CheckExact@29` |
| `complexobject.h` | `PyComplex_Check@13`; `PyComplex_CheckExact@14` |
| `cpython/abstract.h` | `PySequence_Fast_GET_SIZE@91`; `PySequence_Fast_GET_ITEM@96` |
| `cpython/bytearrayobject.h` | `_PyByteArray_CAST@17`; `PyByteArray_AS_STRING@28`; `PyByteArray_GET_SIZE@38` |
| `cpython/bytesobject.h` | `_PyBytes_CAST@20`; `PyBytes_AS_STRING@27`; `PyBytes_GET_SIZE@29`; `PyBytes_GET_SIZE@33` |
| `cpython/cellobject.h` | `PyCell_Check@18`; `PyCell_GET@34`; `PyCell_SET@44` |
| `cpython/classobject.h` | `PyMethod_Check@22`; `_PyMethod_CAST@29`; `PyMethod_GET_FUNCTION@37`; `PyMethod_GET_SELF@42`; `PyInstanceMethod_Check@51`; `_PyInstanceMethod_CAST@56`; `PyInstanceMethod_GET_FUNCTION@65` |
| `cpython/code.h` | `_PyCode_DEF@45`; `PyCode_Check@164`; `PyCode_GetFirstFree@176` |
| `cpython/context.h` | `PyContext_CheckExact@18`; `PyContextVar_CheckExact@19`; `PyContextToken_CheckExact@20` |
| `cpython/dictobject.h` | `PyDict_GET_SIZE@63` |
| `cpython/floatobject.h` | `_PyFloat_CAST@10`; `PyFloat_AS_DOUBLE@18` |
| `cpython/funcobject.h` | `PyFunction_Check@68`; `_PyFunction_CAST@85`; `PyFunction_GET_CODE@93`; `PyFunction_GET_GLOBALS@98`; `PyFunction_GET_MODULE@103`; `PyFunction_GET_DEFAULTS@108`; `PyFunction_GET_KW_DEFAULTS@113`; `PyFunction_GET_CLOSURE@118`; `PyFunction_GET_ANNOTATIONS@123` |
| `cpython/genobject.h` | `PyGen_Check@16`; `PyGen_CheckExact@17`; `PyCoro_CheckExact@31`; `PyAsyncGen_CheckExact@46`; `PyAsyncGenASend_CheckExact@48` |
| `cpython/listobject.h` | `_PyList_CAST@25`; `PyList_GET_SIZE@38`; `PyList_SET_ITEM@49` |
| `cpython/lock.h` | `PyMutex_Lock@55`; `PyMutex_Unlock@66`; `PyMutex_IsLocked@74` |
| `cpython/longintrepr.h` | `PyUnstable_Long_IsCompact@127`; `PyUnstable_Long_CompactValue@139` |
| `cpython/longobject.h` | `_PyLong_CAST@5` |
| `cpython/memoryobject.h` | `PyMemoryView_GET_BUFFER@44`; `PyMemoryView_GET_BASE@50` |
| `cpython/methodobject.h` | `_PyCFunctionObject_CAST@16`; `_PyCMethodObject_CAST@28`; `PyCMethod_CheckExact@34`; `PyCMethod_Check@35`; `PyCFunction_GET_FUNCTION@43`; `PyCFunction_GET_SELF@52`; `PyCFunction_GET_FLAGS@57`; `PyCFunction_GET_CLASS@66` |
| `cpython/monitoring.h` | `PyMonitoring_FirePyStartEvent@125`; `PyMonitoring_FirePyResumeEvent@133`; `PyMonitoring_FirePyReturnEvent@141`; `PyMonitoring_FirePyYieldEvent@150`; `PyMonitoring_FireCallEvent@159`; `PyMonitoring_FireLineEvent@168`; `PyMonitoring_FireJumpEvent@177`; `PyMonitoring_FireBranchRightEvent@186`; `PyMonitoring_FireBranchLeftEvent@195`; `PyMonitoring_FireCReturnEvent@204`; `PyMonitoring_FirePyThrowEvent@213`; `PyMonitoring_FireRaiseEvent@221`; `PyMonitoring_FireReraiseEvent@229`; `PyMonitoring_FireExceptionHandledEvent@237`; `PyMonitoring_FireCRaiseEvent@245`; `PyMonitoring_FirePyUnwindEvent@253`; `PyMonitoring_FireStopIterationEvent@261` |
| `cpython/object.h` | `_Py_static_string@55`; `_Py_IDENTIFIER@56`; `Py_SETREF@342`; `Py_SETREF@350`; `Py_XSETREF@364`; `Py_XSETREF@372` |
| `cpython/odictobject.h` | `PyODict_Check@21`; `PyODict_CheckExact@22`; `PyODict_SIZE@23` |
| `cpython/picklebufobject.h` | `PyPickleBuffer_Check@15` |
| `cpython/pyerrors.h` | `PyException_HEAD@8` |
| `cpython/pyframe.h` | `PyFrame_Check@8`; `PyFrameLocalsProxy_Check@9` |
| `cpython/setobject.h` | `_PySet_CAST@61`; `PySet_GET_SIZE@71` |
| `cpython/tupleobject.h` | `_PyTuple_CAST@18`; `PyTuple_GET_SIZE@23`; `PyTuple_GET_SIZE@27`; `PyTuple_SET_ITEM@39` |
| `cpython/unicodeobject.h` | `_PyASCIIObject_CAST@187`; `_PyCompactUnicodeObject_CAST@190`; `_PyUnicodeObject_CAST@193`; `PyUnicode_CHECK_INTERNED@216`; `PyUnicode_IS_ASCII@229`; `PyUnicode_IS_COMPACT@236`; `PyUnicode_IS_COMPACT_ASCII@243`; `_PyUnicode_DATA@280`; `PyUnicode_DATA@286`; `PyUnicode_1BYTE_DATA@293`; `PyUnicode_2BYTE_DATA@294`; `PyUnicode_4BYTE_DATA@295`; `PyUnicode_GET_LENGTH@301`; `PyUnicode_READ_CHAR@354`; `PyUnicode_READ_CHAR@372`; `PyUnicode_MAX_CHAR_VALUE@378`; `PyUnicode_MAX_CHAR_VALUE@396` |
| `cpython/weakrefobject.h` | `_PyWeakref_CAST@45`; `PyWeakref_GET_OBJECT@66` |
| `datetime.h` | `_PyTZINFO_HEAD@52`; `_PyDateTime_TIMEHEAD@72`; `_PyDateTime_DATETIMEHEAD@100` |
| `dictobject.h` | `PyDict_Check@17`; `PyDict_CheckExact@19`; `PyDictKeys_Check@81`; `PyDictValues_Check@82`; `PyDictItems_Check@83`; `PyDictViewSet_Check@85` |
| `floatobject.h` | `PyFloat_Check@16`; `PyFloat_CheckExact@17` |
| `iterobject.h` | `PySeqIter_Check@11`; `PyCallIter_Check@16` |
| `listobject.h` | `PyList_Check@24`; `PyList_CheckExact@26` |
| `longobject.h` | `PyLong_Check@12`; `PyLong_CheckExact@14` |
| `memoryobject.h` | `PyMemoryView_Check@11` |
| `methodobject.h` | `PyCFunction_CheckExact@16`; `PyCFunction_Check@17` |
| `moduleobject.h` | `PyModule_Check@12`; `PyModule_CheckExact@13`; `PyModuleDef_HEAD_INIT@60` |
| `object.h` | `Py_TYPE@273`; `Py_TYPE@275`; `Py_SIZE@289`; `Py_IS_TYPE@292`; `Py_IS_TYPE@296`; `Py_SET_TYPE@304`; `Py_SET_SIZE@317`; `PyObject_TypeCheck@420`; `PyObject_TypeCheck@424`; `Py_RETURN_NONE@660`; `Py_RETURN_NONE@662`; `Py_RETURN_RICHCOMPARE@702`; `PyType_FastSubclass@793`; `PyType_Check@795`; `PyType_Check@799`; `_PyType_CAST@802`; `PyType_CheckExact@805`; `PyType_CheckExact@809` |
| `objimpl.h` | `PyType_IS_GC@157` |
| `py_curses.h` | `PyCursesWindow_Type@96`; `PyCursesWindow_Check@101` |
| `pycapsule.h` | `PyCapsule_CheckExact@25` |
| `pyerrors.h` | `PyExceptionClass_Check@60`; `PyExceptionInstance_Check@64`; `PyExceptionInstance_Class@69`; `_PyBaseExceptionGroup_Check@71` |
| `pystats.h` | `_Py_INCREF_STAT_INC@19`; `_Py_DECREF_STAT_INC@20`; `_Py_INCREF_IMMORTAL_STAT_INC@21`; `_Py_DECREF_IMMORTAL_STAT_INC@22` |
| `rangeobject.h` | `PyRange_Check@22` |
| `refcount.h` | `Py_REFCNT@119`; `_Py_IsImmortal@134`; `_Py_IsStaticImmortal@145`; `Py_SET_REFCNT@196`; `Py_INCREF@304`; `Py_DECREF@325`; `Py_DECREF@332`; `Py_DECREF@358`; `Py_DECREF@380`; `Py_DECREF@405`; `Py_DECREF@422`; `Py_CLEAR@475`; `Py_CLEAR@485`; `Py_XINCREF@499`; `Py_XINCREF@506`; `Py_XDECREF@509`; `Py_XDECREF@516`; `_Py_NewRef@526`; `_Py_XNewRef@532`; `Py_NewRef@542`; `Py_XNewRef@543`; `Py_NewRef@545`; `Py_XNewRef@546` |
| `setobject.h` | `PyFrozenSet_CheckExact@23`; `PyFrozenSet_Check@24`; `PyAnySet_CheckExact@28`; `PyAnySet_Check@30`; `PySet_CheckExact@35`; `PySet_Check@36` |
| `sliceobject.h` | `PySlice_Check@35` |
| `traceback.h` | `PyTraceBack_Check@14` |
| `tupleobject.h` | `PyTuple_Check@26`; `PyTuple_CheckExact@28` |
| `unicodeobject.h` | `PyUnicode_Check@106`; `PyUnicode_CheckExact@108` |
| `weakrefobject.h` | `PyWeakref_CheckRef@15`; `PyWeakref_CheckRefExact@16`; `PyWeakref_CheckProxy@18`; `PyWeakref_Check@22` |
### B.2 Internal definitions (graph also includes internal headers)

228 definitions; prefix paths with `vendor/cpython/Include/`.

| Header | Wrapper / alias definitions |
| --- | --- |
| `internal/mimalloc/mimalloc/internal.h` | `_mi_ptr_page@450`; `mi_page_usable_block_size@474`; `mi_segment_end@483`; `mi_is_in_same_page@604`; `_mi_memid_none@738` |
| `internal/pycore_atexit.h` | `_PyAtExit_LockCallbacks@18`; `_PyAtExit_UnlockCallbacks@19` |
| `internal/pycore_backoff.h` | `initial_jump_backoff_counter@102`; `initial_temperature_backoff_counter@116`; `initial_unreachable_backoff_counter@124` |
| `internal/pycore_call.h` | `_PyObject_VectorcallTstate@153`; `_PyObject_CallNoArgsTstate@174`; `_PyObject_CallNoArgs@181` |
| `internal/pycore_cell.h` | `PyCell_SetTakeRef@29` |
| `internal/pycore_ceval.h` | `_PyEval_GetExecutableCode@182`; `_Py_EnterRecursiveCallTstate@214`; `_Py_EnterRecursiveCall@219` |
| `internal/pycore_ceval_state.h` | `_PyEval_RUNTIME_PERF_INIT@41` |
| `internal/pycore_code.h` | `_PyCode_NBYTES@17`; `_Py_MAKE_CODEUNIT@40`; `_Py_SET_OPCODE@41`; `_PyLocals_GetKind@191`; `_PyLocals_SetKind@200`; `adaptive_counter_bits@458`; `adaptive_counter_warmup@463`; `adaptive_counter_cooldown@469`; `adaptive_counter_backoff@475` |
| `internal/pycore_critical_section.h` | `Py_BEGIN_CRITICAL_SECTION_SEQUENCE_FAST@29`; `Py_END_CRITICAL_SECTION_SEQUENCE_FAST@38`; `_Py_CRITICAL_SECTION_ASSERT_MUTEX_LOCKED@47`; `_Py_CRITICAL_SECTION_ASSERT_OBJECT_LOCKED@62`; `Py_BEGIN_CRITICAL_SECTION_SEQUENCE_FAST@68`; `Py_END_CRITICAL_SECTION_SEQUENCE_FAST@69`; `_Py_CRITICAL_SECTION_ASSERT_MUTEX_LOCKED@70`; `_Py_CRITICAL_SECTION_ASSERT_OBJECT_LOCKED@71`; `PyCriticalSection_BeginMutex@110`; `PyCriticalSection_Begin@117`; `PyCriticalSection_End@146`; `PyCriticalSection2_BeginMutex@186`; `PyCriticalSection2_Begin@193`; `PyCriticalSection2_End@212` |
| `internal/pycore_debug_offsets.h` | `_Py_Debug_interpreter_state_tlbc_generation@63` |
| `internal/pycore_dict.h` | `DK_SIZE@237`; `DK_SIZE@239`; `_Py_INCREF_DICT@349`; `_Py_DECREF_DICT@350`; `_Py_INCREF_BUILTINS@351`; `_Py_DECREF_BUILTINS@352`; `_Py_INCREF_DICT@360`; `_Py_DECREF_DICT@368`; `_Py_INCREF_BUILTINS@378`; `_Py_DECREF_BUILTINS@389` |
| `internal/pycore_dtoa.h` | `_dtoa_state_INIT@16` |
| `internal/pycore_fileutils.h` | `DECODE_LOCALE_ERR@188` |
| `internal/pycore_freelist.h` | `_PyFreeList_Free@64`; `_PyFreeList_Pop@85`; `_PyFreeList_PopMem@96` |
| `internal/pycore_function.h` | `_PyFunction_GET_BUILTINS@47` |
| `internal/pycore_gc.h` | `_PyObject_GC_IS_TRACKED@81`; `_PyObject_GC_MAY_BE_TRACKED@85`; `_PyObject_GC_IS_SHARED@103`; `_PyObject_GC_IS_SHARED@106`; `_PyObject_GC_SET_SHARED@108`; `_PyObject_GC_SET_SHARED@111` |
| `internal/pycore_global_objects_fini_generated.h` | `_PyStaticObject_CheckRefcnt@12` |
| `internal/pycore_hamt.h` | `PyHamt_Check@35` |
| `internal/pycore_import.h` | `DLOPENFLAGS_INIT@44` |
| `internal/pycore_initconfig.h` | `_PyStatus_NO_MEMORY@33` |
| `internal/pycore_instruction_sequence.h` | `IS_JUMP_TARGET_LABEL@60`; `_PyInstructionSequence_Check@78` |
| `internal/pycore_interpframe_structs.h` | `_PyGenObject_HEAD@59` |
| `internal/pycore_interpolation.h` | `_PyInterpolation_CheckExact@14` |
| `internal/pycore_long.h` | `_PyLong_SMALL_INTS@59`; `_PyLong_GetZero@71`; `_PyLong_GetOne@76`; `_PyLong_FromUnsignedChar@79` |
| `internal/pycore_object.h` | `_Py_IsImmortalLoose@31`; `_Py_RefcntAdd@179`; `_Py_ClearImmortal@218`; `_Py_ClearImmortal@225`; `_Py_DECREF_SPECIALIZED@276`; `_Py_DECREF_NO_DEALLOC@282`; `_Py_INCREF_TYPE@340`; `_Py_DECREF_TYPE@341`; `_Py_INCREF_CODE@342`; `_Py_DECREF_CODE@343`; `Py_DECREF_MORTAL@453`; `Py_DECREF_MORTAL_SPECIALIZED@473`; `Py_DECREF_MORTAL@485`; `Py_DECREF_MORTAL_SPECIALIZED@496`; `_PyObject_Init@506`; `_PyObject_InitVar@516`; `_PyObject_GC_TRACK@529`; `_PyObject_GC_UNTRACK@531`; `_PyObject_GC_TRACK@534`; `_PyObject_GC_UNTRACK@536`; `_Py_TryIncrefCompare@594`; `_Py_XGetRef@613`; `_Py_TryXGetRef@629`; `_Py_XNewRefWithLock@669`; `_Py_TryIncref@756`; `_PyType_IS_GC@829`; `_PyType_PreHeaderSize@854` |
| `internal/pycore_object_deferred.h` | `_PyObject_HasDeferredRefcount@19` |
| `internal/pycore_obmalloc_init.h` | `PT@17`; `PT_8@19`; `_obmalloc_pools_INIT@30`; `_obmalloc_pools_INIT@33`; `_obmalloc_pools_INIT@36`; `_obmalloc_pools_INIT@39`; `_obmalloc_pools_INIT@42`; `_obmalloc_pools_INIT@45`; `_obmalloc_pools_INIT@48`; `_obmalloc_pools_INIT@51` |
| `internal/pycore_opcode_utils.h` | `HAS_TARGET@22`; `IS_TERMINATOR_OPCODE@26` |
| `internal/pycore_pyhash.h` | `_py_urandom_cache_INIT@80` |
| `internal/pycore_pymem.h` | `_PyObject_XDecRefDelayed@97` |
| `internal/pycore_pymem_init.h` | `_pymem_allocators_standard_INIT@67`; `_pymem_allocators_standard_INIT@75` |
| `internal/pycore_runtime_init.h` | `_PyBytes_CHAR_INIT@200` |
| `internal/pycore_runtime_init_generated.h` | `_Py_small_ints_INIT@15`; `_Py_bytes_characters_INIT@280`; `_Py_str_literals_INIT@539`; `_Py_str_identifiers_INIT@565`; `_Py_str_ascii_INIT@1323`; `_Py_str_latin1_INIT@1454` |
| `internal/pycore_stackref.h` | `PyStackRef_IsTrue@80`; `PyStackRef_IsFalse@86`; `PyStackRef_IsNone@92`; `PyStackRef_AsPyObjectBorrow@106`; `PyStackRef_AsPyObjectSteal@113`; `_PyStackRef_FromPyObjectNew@115`; `PyStackRef_FromPyObjectNew@121`; `_PyStackRef_FromPyObjectSteal@123`; `PyStackRef_FromPyObjectSteal@128`; `_PyStackRef_FromPyObjectImmortal@130`; `PyStackRef_FromPyObjectImmortal@136`; `_PyStackRef_CLOSE@144`; `PyStackRef_CLOSE@153`; `_PyStackRef_XCLOSE@156`; `PyStackRef_XCLOSE@164`; `_PyStackRef_DUP@166`; `PyStackRef_DUP@178`; `PyStackRef_CLOSE_SPECIALIZED@181`; `PyStackRef_MakeHeapSafe@183`; `PyStackRef_Borrow@189`; `PyStackRef_CLEAR@195`; `_PyStackRef_FromPyObjectStealMortal@203`; `PyStackRef_FromPyObjectStealMortal@209`; `PyStackRef_IsHeapSafe@211`; `_PyStackRef_FromPyObjectNewMortal@217`; `PyStackRef_FromPyObjectNewMortal@224`; `PyStackRef_RefcountOnObject@226`; `PyStackRef_IsNone@281`; `PyStackRef_IsTrue@282`; `PyStackRef_IsFalse@283`; `PyStackRef_IsNullOrInt@285`; `PyStackRef_AsPyObjectSteal@303`; `PyStackRef_FromPyObjectSteal@321`; `PyStackRef_IsHeapSafe@323`; `PyStackRef_FromPyObjectNew@366`; `PyStackRef_FromPyObjectImmortal@377`; `PyStackRef_CLOSE@379`; `PyStackRef_CLOSE_SPECIALIZED@388`; `PyStackRef_DUP@395`; `PyStackRef_AsStrongReference@413`; `PyStackRef_XCLOSE@419`; `PyStackRef_CLEAR@427`; `PyStackRef_FromPyObjectNewMortal@437`; `PyStackRef_CheckValid@486`; `PyStackRef_AsPyObjectBorrow@492`; `PyStackRef_AsPyObjectBorrow@502`; `PyStackRef_AsPyObjectSteal@516`; `PyStackRef_FromPyObjectNew@563`; `PyStackRef_FromPyObjectNewMortal@574`; `PyStackRef_DUP@586`; `PyStackRef_DUP@589`; `PyStackRef_CLOSE@620`; `PyStackRef_CLOSE@626`; `PyStackRef_IsNullOrInt@636`; `PyStackRef_CLOSE_SPECIALIZED@642`; `PyStackRef_XCLOSE@652`; `PyStackRef_CLEAR@665`; `PyStackRef_TYPE@683`; `PyStackRef_AsPyObjectNew@687`; `PyStackRef_GenCheck@691`; `PyStackRef_BoolCheck@697`; `PyStackRef_LongCheck@703`; `PyStackRef_ExceptionInstanceCheck@709`; `PyStackRef_CodeCheck@715`; `PyStackRef_FunctionCheck@721`; `_Py_TryXGetStackRef@765`; `_Py_VISIT_STACKREF@779` |
| `internal/pycore_stats.h` | `STAT_INC@56`; `STAT_DEC@57`; `OPCODE_EXE_INC@58`; `CALL_STAT_INC@59`; `OBJECT_STAT_INC@60`; `OBJECT_STAT_INC_COND@61`; `EVAL_CALL_STAT_INC@62`; `EVAL_CALL_STAT_INC_IF_FUNCTION@63`; `GC_STAT_ADD@64`; `OPT_STAT_INC@65`; `OPT_STAT_ADD@66`; `UOP_STAT_INC@67`; `UOP_PAIR_INC@68`; `OPT_UNSUPPORTED_OPCODE@69`; `OPT_ERROR_IN_OPCODE@70`; `OPT_HIST@71`; `RARE_EVENT_STAT_INC@72`; `OPCODE_DEFERRED_INC@73`; `RARE_EVENT_INC@87` |
| `internal/pycore_symtable.h` | `PySTEntry_Check@138` |
| `internal/pycore_template.h` | `_PyTemplate_CheckExact@15`; `_PyTemplateIter_CheckExact@16` |
| `internal/pycore_tuple.h` | `_PyTuple_Recycle@44` |
| `internal/pycore_unicodeobject_generated.h` | `_PyUnicode_InitStaticStrings@12` |
| `internal/pycore_unionobject.h` | `_PyUnion_Check@15`; `_PyGenericAlias_Check@17` |
| `internal/pycore_weakref.h` | `LOCK_WEAKREFS@23`; `UNLOCK_WEAKREFS@25`; `FT_CLEAR_WEAKREFS@32`; `LOCK_WEAKREFS@40`; `UNLOCK_WEAKREFS@41`; `LOCK_WEAKREFS_FOR_WR@43`; `UNLOCK_WEAKREFS_FOR_WR@44`; `FT_CLEAR_WEAKREFS@46` |

## Appendix C. Complete nonzero NumPy use counts

Whole-token code counts; C includes `.c` and `.c.src`, C++ includes `.cpp`, `.cpp.src`, `.cc`, `.cxx`, H includes `.h`, `.h.src`, `.hpp`, `.hpp.src`. Strings/comments are masked, but macro definitions and inactive branches remain. Every extension-facing identifier selected by the Appendix B graph with a nonzero count appears below; all other selected identifiers have zero occurrences in these files. Raw C differs where comments/literals mention the identifier. “First location” is one reproducible lexical hit, which can be a compatibility header/test rather than production code. Prefix those locations with `vendor/numpy/`. Includes bundled sources; does not include generated Cython outputs absent from the checkout.

| Identifier | C | C++ | H | Raw C | First location |
| --- | ---: | ---: | ---: | ---: | --- |
| `PyBool_Check` | 12 | 0 | 1 | 12 | `numpy/_core/include/numpy/ndarrayobject.h:47` |
| `PyBytes_AS_STRING` | 8 | 0 | 4 | 8 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:2103` |
| `PyBytes_Check` | 48 | 0 | 2 | 48 | `numpy/_core/include/numpy/ndarrayobject.h:51` |
| `PyBytes_CheckExact` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/multiarraymodule.c:3626` |
| `PyBytes_GET_SIZE` | 8 | 0 | 3 | 8 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:2542` |
| `PyCapsule_CheckExact` | 4 | 2 | 1 | 4 | `numpy/_core/include/numpy/npy_3kcompat.h:375` |
| `PyCode_Check` | 1 | 0 | 0 | 1 | `numpy/_core/src/common/pythoncapi-compat/tests/test_pythoncapi_compat_cext.c:217` |
| `PyComplex_Check` | 4 | 1 | 1 | 4 | `numpy/_core/include/numpy/ndarrayobject.h:46` |
| `PyComplex_CheckExact` | 5 | 0 | 1 | 5 | `numpy/_core/src/multiarray/abstractdtypes.h:111` |
| `PyDateTime_Check` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/datetime.c:3303` |
| `PyDateTime_CheckExact` | 3 | 0 | 0 | 3 | `numpy/_core/src/multiarray/datetime.c:2130` |
| `PyDateTime_DATE_GET_HOUR` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2137` |
| `PyDateTime_DATE_GET_MICROSECOND` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2140` |
| `PyDateTime_DATE_GET_MINUTE` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2138` |
| `PyDateTime_DATE_GET_SECOND` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2139` |
| `PyDateTime_DATE_GET_TZINFO` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2255` |
| `PyDateTime_DELTA_GET_DAYS` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2787` |
| `PyDateTime_DELTA_GET_MICROSECONDS` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2789` |
| `PyDateTime_DELTA_GET_SECONDS` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2788` |
| `PyDateTime_FromDateAndTime` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/datetime.c:2942` |
| `PyDateTime_GET_DAY` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2133` |
| `PyDateTime_GET_MONTH` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2132` |
| `PyDateTime_GET_YEAR` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2131` |
| `PyDateTime_TimeZone_UTC` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2342` |
| `PyDate_Check` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/datetime.c:3302` |
| `PyDate_CheckExact` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2130` |
| `PyDate_FromDate` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2947` |
| `PyDelta_Check` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/datetime.c:3316` |
| `PyDelta_CheckExact` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/datetime.c:2785` |
| `PyDelta_FromDSU` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/datetime.c:3124` |
| `PyDict_Check` | 11 | 0 | 1 | 11 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:1087` |
| `PyDict_CheckExact` | 6 | 0 | 0 | 6 | `numpy/_core/src/common/ufunc_override.c:91` |
| `PyDict_GET_SIZE` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/array_converter.c:42` |
| `PyExceptionInstance_Class` | 0 | 1 | 0 | 0 | `numpy/_core/src/multiarray/stringdtype/casts.cpp:2091` |
| `PyFloat_AS_DOUBLE` | 5 | 2 | 0 | 5 | `numpy/_core/src/multiarray/arraytypes.c.src:120` |
| `PyFloat_Check` | 7 | 1 | 1 | 7 | `numpy/_core/include/numpy/ndarrayobject.h:46` |
| `PyFloat_CheckExact` | 7 | 0 | 1 | 7 | `numpy/_core/src/multiarray/abstractdtypes.h:104` |
| `PyFrame_Check` | 2 | 0 | 0 | 2 | `numpy/_core/src/common/pythoncapi-compat/tests/test_pythoncapi_compat_cext.c:231` |
| `PyList_Check` | 16 | 0 | 0 | 16 | `numpy/_core/src/multiarray/conversion_utils.c:821` |
| `PyList_CheckExact` | 1 | 0 | 1 | 1 | `numpy/_core/src/common/npy_pycompat.h:67` |
| `PyList_GET_ITEM` | 8 | 0 | 0 | 9 | `numpy/_core/src/multiarray/ctors.c:2116` |
| `PyList_GET_SIZE` | 7 | 0 | 0 | 8 | `numpy/_core/src/multiarray/ctors.c:2113` |
| `PyList_SET_ITEM` | 6 | 0 | 0 | 6 | `numpy/_core/src/multiarray/arrayfunction_override.c:404` |
| `PyLong_Check` | 20 | 0 | 12 | 20 | `numpy/_core/include/numpy/ndarrayobject.h:47` |
| `PyLong_CheckExact` | 14 | 1 | 1 | 14 | `numpy/_core/src/multiarray/abstractdtypes.h:97` |
| `PyMemoryView_Check` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/arraytypes.c.src:796` |
| `PyMemoryView_GET_BASE` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/conversion_utils.c:334` |
| `PyMemoryView_GET_BUFFER` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/ctors.c:1199` |
| `PyModuleDef_HEAD_INIT` | 23 | 2 | 0 | 24 | `numpy/_core/src/_simd/_simd.c:135` |
| `PyModule_Check` | 2 | 0 | 1 | 2 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:469` |
| `PyMutex_IsLocked` | 2 | 0 | 0 | 2 | `numpy/_core/src/common/npy_hashtable.c:200` |
| `PyMutex_Lock` | 4 | 1 | 0 | 4 | `numpy/_core/src/common/npy_argparse.c:25` |
| `PyMutex_Unlock` | 4 | 1 | 0 | 4 | `numpy/_core/src/common/npy_argparse.c:26` |
| `PyObject_HEAD` | 5 | 0 | 41 | 8 | `numpy/_core/include/numpy/arrayscalars.h:7` |
| `PyObject_HEAD_INIT` | 5 | 0 | 0 | 5 | `numpy/_core/src/multiarray/arraytypes.c.src:4187` |
| `PyObject_TypeCheck` | 19 | 10 | 4 | 19 | `numpy/_core/include/numpy/ndarrayobject.h:23` |
| `PyObject_VAR_HEAD` | 0 | 0 | 2 | 0 | `numpy/_core/include/numpy/arrayscalars.h:152` |
| `PySequence_Fast_GET_ITEM` | 5 | 1 | 0 | 5 | `numpy/_core/src/multiarray/conversion_utils.c:1152` |
| `PySequence_Fast_GET_SIZE` | 10 | 0 | 0 | 10 | `numpy/_core/src/common/ufunc_override.c:121` |
| `PySequence_Fast_ITEMS` | 8 | 0 | 0 | 8 | `numpy/_core/src/common/ufunc_override.c:119` |
| `PySlice_Check` | 3 | 0 | 0 | 3 | `numpy/_core/src/multiarray/mapping.c:338` |
| `PyTuple_Check` | 65 | 0 | 1 | 65 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:666` |
| `PyTuple_CheckExact` | 18 | 1 | 0 | 18 | `numpy/_core/src/common/pythoncapi-compat/tests/test_pythoncapi_compat_cext.c:639` |
| `PyTuple_GET_ITEM` | 119 | 21 | 1 | 119 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:708` |
| `PyTuple_GET_SIZE` | 88 | 3 | 1 | 88 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:674` |
| `PyTuple_SET_ITEM` | 99 | 4 | 4 | 99 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:690` |
| `PyType_Check` | 9 | 0 | 0 | 9 | `numpy/_core/src/multiarray/ctors.c:2029` |
| `PyUnicode_1BYTE_DATA` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/textreading/stream_pyobject.c:74` |
| `PyUnicode_2BYTE_DATA` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/textreading/stream_pyobject.c:77` |
| `PyUnicode_4BYTE_DATA` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/textreading/stream_pyobject.c:81` |
| `PyUnicode_Check` | 74 | 0 | 5 | 74 | `numpy/_core/include/numpy/ndarrayobject.h:52` |
| `PyUnicode_CheckExact` | 4 | 0 | 1 | 4 | `numpy/_core/src/multiarray/abstractdtypes.h:131` |
| `PyUnicode_DATA` | 0 | 0 | 1 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:1014` |
| `PyUnicode_GET_LENGTH` | 2 | 0 | 2 | 2 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:1015` |
| `PyUnicode_IS_ASCII` | 3 | 0 | 1 | 3 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:1013` |
| `PyUnicode_KIND` | 2 | 0 | 0 | 2 | `numpy/_core/src/multiarray/scalartypes.c.src:3825` |
| `PyUnicode_READ_CHAR` | 1 | 0 | 0 | 1 | `numpy/_core/src/multiarray/textreading/readtext.c:98` |
| `PyVarObject_HEAD_INIT` | 18 | 0 | 0 | 18 | `numpy/_core/src/multiarray/array_method.c:569` |
| `PyWeakref_Check` | 0 | 0 | 1 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:612` |
| `Py_CLEAR` | 63 | 3 | 1 | 65 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:981` |
| `Py_DECREF` | 1941 | 123 | 69 | 1946 | `numpy/_core/include/numpy/ndarrayobject.h:154` |
| `Py_INCREF` | 594 | 116 | 16 | 595 | `numpy/_core/include/numpy/arrayscalars.h:207` |
| `Py_IS_TYPE` | 3 | 0 | 2 | 4 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:513` |
| `Py_NewRef` | 66 | 8 | 22 | 67 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:93` |
| `Py_REFCNT` | 40 | 0 | 4 | 42 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:2017` |
| `Py_RETURN_FALSE` | 22 | 0 | 0 | 22 | `numpy/_core/src/multiarray/_multiarray_tests.c.src:77` |
| `Py_RETURN_NONE` | 124 | 0 | 0 | 124 | `numpy/_core/src/_simd/_simd.c:15` |
| `Py_RETURN_TRUE` | 22 | 0 | 0 | 22 | `numpy/_core/src/multiarray/_multiarray_tests.c.src:74` |
| `Py_SETREF` | 84 | 3 | 2 | 85 | `numpy/_core/src/common/npy_pycompat.h:12` |
| `Py_SET_REFCNT` | 2 | 0 | 2 | 3 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:115` |
| `Py_SET_SIZE` | 3 | 0 | 3 | 4 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:151` |
| `Py_SET_TYPE` | 10 | 0 | 3 | 12 | `numpy/_core/include/numpy/npy_2_compat.h:375` |
| `Py_SIZE` | 3 | 0 | 3 | 3 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:1886` |
| `Py_TYPE` | 182 | 3 | 22 | 184 | `numpy/_core/include/numpy/dtype_api.h:523` |
| `Py_XDECREF` | 718 | 18 | 21 | 719 | `numpy/_core/include/numpy/ndarrayobject.h:164` |
| `Py_XINCREF` | 54 | 3 | 7 | 54 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:107` |
| `Py_XNewRef` | 4 | 0 | 7 | 5 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:104` |
| `Py_XSETREF` | 32 | 9 | 2 | 33 | `numpy/_core/src/common/npy_pycompat.h:33` |
| `_PyUnicodeWriter_Prepare` | 0 | 0 | 1 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:1416` |
| `_Py_INCREF_STAT_INC` | 0 | 0 | 2 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:2390` |
| `_Py_IsImmortal` | 0 | 0 | 1 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:2425` |
| `_Py_IsOwnedByCurrentThread` | 0 | 0 | 2 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:2362` |
| `_Py_NewRef` | 0 | 0 | 2 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:94` |
| `_Py_XNewRef` | 0 | 0 | 2 | 0 | `numpy/_core/src/common/pythoncapi-compat/pythoncapi_compat.h:105` |

The following out-of-line APIs are intentionally additional to the macro/inline closure; their ownership semantics matter even though their bodies are not baked in:

| API | C | C++ | H |
| --- | ---: | ---: | ---: |
| `PyUnstable_Object_IsUniquelyReferenced` | 4 | 0 | 2 |
| `PyUnstable_Object_IsUniqueReferencedTemporary` | 1 | 0 | 0 |
| `PyList_GetItemRef` | 1 | 0 | 1 |
| `PyDict_GetItemRef` | 15 | 2 | 3 |
| `PyWeakref_GetRef` | 4 | 0 | 1 |
| `PyVectorcall_Function` | 0 | 0 | 0 |
| `PyObject_Vectorcall` | 31 | 1 | 1 |
| `PyObject_GC_Track` | 1 | 0 | 0 |
| `PyObject_GC_UnTrack` | 8 | 0 | 1 |
| `PyObject_GC_Del` | 3 | 0 | 0 |
| `PyCriticalSection_Begin` | 0 | 0 | 1 |
| `PyCriticalSection_End` | 0 | 0 | 1 |
| `Py_BEGIN_CRITICAL_SECTION` | 4 | 0 | 3 |
| `Py_END_CRITICAL_SECTION` | 4 | 0 | 3 |

## Appendix D. Reproduce the lexical census

Pass the Lucifer repository source root as the sole argument to this standalone Python 3 analysis program (for example, `python3 census.py .` after saving the block as `census.py`). It reads vendor files as text, never imports or executes them, writes no files, and prints JSON backing the inventory/count tables. The scanner deliberately retains branches and computes token reachability rather than pretending to preprocess arbitrary build configurations. The explicit initializer/payload seeds are visible in `extra`. The output does not include the separately reviewed implicit C++ allocator methods described after Appendix A.

```python
import re,pathlib,collections,json,sys
if len(sys.argv)!=2:
 raise SystemExit('usage: census.py SOURCE_ROOT')
source_root=pathlib.Path(sys.argv[1])
root=source_root/'vendor/cpython/Include'
def mask(s):
 return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),s)
defs=[]
for p in sorted(root.rglob('*.h')):
 s=p.read_text(); t=mask(s)
 spans=[]
 for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)([^\n]*(?:\\\n[^\n]*)*)',t,re.M):
  # Extend through escaped physical lines; greedy first-line part otherwise hides newline.
  a=m.start(); b=t.find('\n',a)
  if b<0:b=len(t)
  while b<len(t) and t[a:b].rstrip().endswith('\\'):
   b=t.find('\n',b+1)
   if b<0:b=len(t)
  text=t[a:b]; name=m[1]; spans.append((a,b))
  defs.append(dict(path=str(p.relative_to(root)),name=name,line=t.count('\n',0,a)+1,kind='macro',body=text))
 chars=list(t)
 for a,b in spans:
  chars[a:b]=['\n' if c=='\n' else ' ' for c in t[a:b]]
 u=''.join(chars)
 for m in re.finditer(r'\bstatic\s+(?:inline|__inline__?)\b|\bPy_LOCAL_INLINE\s*\([^)]*\)',u):
  a=m.start(); brace=u.find('{',m.end()); semi=u.find(';',m.end())
  if brace<0 or (semi>=0 and semi<brace):continue
  sig=u[m.end():brace]
  nm=re.search(r'([A-Za-z_]\w*)\s*\(',sig)
  if not nm:continue
  name=nm[1]; level=1; b=brace+1
  while b<len(u) and level:
   level+=(u[b]=='{')-(u[b]=='}'); b+=1
  defs.append(dict(path=str(p.relative_to(root)),name=name,line=u.count('\n',0,a)+1,kind='inline',body=u[a:b]))
for d in defs:
 d['fields']=sorted(set(re.findall(r'(?:->|\.)\s*([A-Za-z_]\w*)',d['body'])))
# Header layout arithmetic and initializer exposure lack explicit field tokens.
extra={'PyObject_HEAD','PyObject_VAR_HEAD','PyObject_HEAD_INIT','PyVarObject_HEAD_INIT','_PyUnicode_COMPACT_DATA','PyUnicode_WRITE','PyUnicode_READ','_PyGC_Head_UNUSED','_Py_AS_GC','_Py_FROM_GC'}
seed={d['name'] for d in defs if d['fields'] or d['name'] in extra}
allnames={d['name'] for d in defs}
deps={n:set() for n in allnames}
for d in defs:deps[d['name']].update(set(re.findall(r'\b[A-Za-z_]\w*\b',d['body'])) & allnames-{d['name']})
affected=set(seed)
while True:
 add={n for n,ds in deps.items() if ds&affected}-affected
 if not add:break
 affected|=add
selected=[d for d in defs if d['name'] in affected]
direct=[d for d in defs if d['fields'] or d['name'] in extra]

extdefs=[d for d in defs if not d['path'].startswith('internal/')]
extall={d['name'] for d in extdefs}
extseed={d['name'] for d in extdefs if d['fields'] or d['name'] in extra}
extdeps={n:set() for n in extall}
for d in extdefs:extdeps[d['name']].update(set(re.findall(r'\b[A-Za-z_]\w*\b',d['body']))&extall-{d['name']})
extaffected=set(extseed)
while True:
 add={n for n,ds in extdeps.items() if ds&extaffected}-extaffected
 if not add:break
 extaffected|=add

sets=collections.defaultdict(list)
for p in sorted((source_root/'vendor/numpy').rglob('*')):
 if not p.is_file(): continue
 n=p.name
 g='C' if n.endswith(('.c','.c.src')) else 'C++' if n.endswith(('.cpp','.cpp.src','.cc','.cxx')) else 'H' if n.endswith(('.h','.h.src','.hpp','.hpp.src')) else None
 if g:sets[g].append(p)
ct={g:collections.Counter() for g in sets};rawct={g:collections.Counter() for g in sets};first={};prod=collections.Counter()
for g,ps in sets.items():
 for p in ps:
  s=p.read_text(errors='replace'); t=mask(s); toks=re.findall(r'\b[A-Za-z_]\w*\b',t)
  ct[g].update(toks);rawct[g].update(re.findall(r'\b[A-Za-z_]\w*\b',s))
  if g=='C' and 'pythoncapi-compat/' not in str(p):prod.update(toks)
  for m in re.finditer(r'\b[A-Za-z_]\w*\b',t):
   if m[0] in extaffected and m[0] not in first:first[m[0]]=(p.relative_to(source_root).as_posix(),t.count('\n',0,m.start())+1)

print(json.dumps({
 "header_files": len(list(root.rglob('*.h'))),
 "field_definitions": sum(bool(d["fields"]) for d in defs),
 "direct_inventory": [{k:d[k] for k in ("path","name","line","kind","fields")}
                      for d in direct],
 "extension_wrappers": [{k:d[k] for k in ("path","name","line","kind")}
                       for d in extdefs if d["name"] in extaffected
                       and not d["fields"] and d["name"] not in extra],
 "internal_wrappers": [{k:d[k] for k in ("path","name","line","kind")}
                      for d in selected if d["path"].startswith("internal/")
                      and not d["fields"] and d["name"] not in extra],
 "numpy_files": {g:len(ps) for g,ps in sets.items()},
 "numpy_api": {n:{"code":{g:ct[g][n] for g in sets},
                 "raw_C":rawct["C"][n], "first":first[n]}
               for n in sorted(extaffected) if any(ct[g][n] for g in sets)},
 "numpy_other": {n:{g:ct[g][n] for g in sets} for n in [
   "PyUnstable_Object_IsUniquelyReferenced",
   "PyUnstable_Object_IsUniqueReferencedTemporary",
   "PyList_GetItemRef","PyDict_GetItemRef","PyWeakref_GetRef",
   "PyVectorcall_Function","PyObject_Vectorcall","PyObject_GC_Track",
   "PyObject_GC_UnTrack","PyObject_GC_Del","PyCriticalSection_Begin",
   "PyCriticalSection_End","Py_BEGIN_CRITICAL_SECTION",
   "Py_END_CRITICAL_SECTION","ob_refcnt","ob_type","ob_size","ob_item",
   "ob_fval","ob_sval","ob_shash","ob_hash","ob_tid","ob_ref_local",
   "ob_ref_shared","lv_tag","ob_digit","tp_flags","tp_basicsize",
   "tp_itemsize","tp_dealloc","tp_as_number","tp_as_sequence",
   "tp_vectorcall_offset","tp_dictoffset","tp_weaklistoffset"]},
 "numpy_C_excluding_compat": {n:prod[n] for n in [
   "Py_INCREF","Py_DECREF","PyTuple_GET_ITEM","Py_REFCNT",
   "PyUnstable_Object_IsUniquelyReferenced",
   "PyUnstable_Object_IsUniqueReferencedTemporary","PyList_GET_ITEM",
   "PyFloat_AS_DOUBLE","ob_type","ob_refcnt","ob_tid","ob_ref_local",
   "ob_ref_shared"]},
 "numpy_CYTHON_marker_files": {
   g:sum("CYTHON_" in p.read_text(errors="replace") for p in ps)
   for g,ps in sets.items()}
}, indent=2))
```
