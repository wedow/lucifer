# 03 — A standard library mostly written in Python

Research date: 2026-11-02. Scope: design input, not an implementation or a performance claim. The fixed requirements in `README.md` remain authoritative: full non-limited C-API from day one, numpy's tests against an unmodified numpy build, no GIL, and built-in types predominantly implemented in Python over native primitives.

**Recommendation:** reuse CPython's Python library and its existing fallback algorithms; implement container and language-visible built-in algorithms in Python against a private, layout-aware primitive interface; retain native VM services, C-API entry stubs, OS interfaces, and established external algorithm libraries. A small primitive *vocabulary* is plausible; a tiny total native implementation is not established. Full C layouts, concurrent mutation, callback ownership, and bootstrap are the constraints that decide whether this division works. Sources: the census below; `vendor/cpython/Include/cpython/{listobject,dictobject,unicodeobject,longintrepr}.h`; `vendor/cpython/Objects/{listobject,dictobject}.c`.

Evidence labels: **measured** means source records counted by the accompanying shell/awk script; **verified** means inspected source or a cited published result; **proposal/estimate — unverified** means a Lucifer design or workload estimate. No vendored program was executed, built, installed, or benchmarked. The Smalltalk VM was inspected only with `git show origin/master:<path>` and `git ls-tree -r --name-only origin/master`. The accompanying counting script is part of this report.

## 1. Reproducible size census

Pinned inputs:

| Tree | Revision | Meaning |
|---|---|---|
| `vendor/cpython` | `ebf955df7a89ed0c7968f79faec1de49f61ed7cb`, tag `v3.14.0` | CPython 3.14.0, all platforms' checked-in sources |
| `vendor/pypy3` | `2e890abd7377b8dc1f5e94064e91c9bbfd8ddcf0`, branch `py3.12` | Targets **Python 3.12.15**, PyPy 8.0.2 alpha: `pypy/module/sys/version.py`; commit dated 2026-11-01 |

Run `sh docs/research/03-stdlib-count.sh .` from the repository, or pass the repository source root explicitly when running the script from elsewhere. Complete stdout is embedded in Appendix A. The script reads source as text, uses shell/find/sort/awk, creates no temporary files, and executes no source from `vendor/`.

| CPython subtree | Files | Physical lines, comments and blanks included |
|---|---:|---:|
| `Objects/**/*.c` | 68 | 123,604 |
| `Python/**/*.c` | 100 | 137,638 |
| `Modules/**/*.c` | 251 | 301,011 |
| `Parser/**/*.c` | 16 | 45,816 |
| **Those four C trees** | **435** | **608,069** |
| `Lib/**/*.py`, excluding exactly `Lib/test/**` | 718 | 329,050 |
| `Lib/test/**/*.py` | 1,112 | 630,844 |

These are physical source records, not executable SLOC or an effort conversion. Checked-in generated `.c` files, bundled libraries, tests under the C trees, and platform alternatives are included; `Include/`, `PC/`, assembly, `.h` and `.inc` are outside the main C totals. Tests inside other `Lib` packages are not silently removed: the requested exclusion is exactly `Lib/test`. Supplementary `.h`/`.inc` counts total **241,216** lines across the four C trees. Algorithmic code lives in headers too: `Modules/_sre/sre_lib.h` alone has **1,874** lines, so its matching engine is larger than `sre.c`'s **3,451** lines. Sources: Appendix A; `vendor/cpython/Modules/_sre/*`.

Appendix A gives two complementary per-module views. The `Modules` path buckets partition all 301,011 `.c` lines exactly once, including support libraries and test fixtures. The Setup recipe table gives direct `.c` translation units per configured module from `Modules/Setup.bootstrap.in` and `Modules/Setup.stdlib.in`. It excludes transitive includes, link libraries and configure-substituted optional source tokens; do not sum it as total implementation size. For example, `_decimal` has **6,335** direct lines, while the `_decimal/` bucket including libmpdec has **21,778**; `_io`'s POSIX recipe has **11,572**, while its cross-platform directory has **12,836**. HACL*, expat and support code remain separately visible rather than being charged repeatedly to callers.

## 2. CPython accelerator and native-module inventory

“Twin” below means production Python implementing the relevant algorithm in this checkout. It does **not** mean no primitive dependencies, identical internal layouts, or proven interchangeability under a no-GIL runtime. A Python public wrapper is not automatically a twin. Inventory scope is deliberately broader than optional accelerators: all **91 production POSIX Setup recipes**, the extra core modules in `Modules/config.c.in`, and Windows-specific modules are accounted for below. Source paths in tables are relative to `vendor/cpython/`.

### Existing Python algorithm implementations

| C module / implementation | Python twin or fallback | Exact boundary |
|---|---|---|
| `_abc`, `Modules/_abc.c` | `Lib/_py_abc.py`, selected by `Lib/abc.py` | ABCMeta algorithm; weak references and synchronization still needed. |
| `_asyncio`, `Modules/_asynciomodule.c` | `Lib/asyncio/futures.py`, `tasks.py`: `_PyFuture`, `_PyTask`, Python task-registration helpers | Future/Task algorithms; event-loop I/O, coroutine execution and context state remain runtime services. |
| `_bisect`, `Modules/_bisectmodule.c` | `Lib/bisect.py` | Both Python algorithms retained before optional C import. |
| `_datetime`, `Modules/_datetimemodule.c` | `Lib/_pydatetime.py`, selected by `Lib/datetime.py` | Date/time arithmetic and formatting; clock/locale/timezone services and the `datetime_CAPI` capsule require explicit support. |
| `_decimal`, `Modules/_decimal/_decimal.c` | `Lib/_pydecimal.py`, selected by `Lib/decimal.py` | Substantial decimal algorithm, **6,389** lines. Uses `ContextVar`; supplying only thread-local storage would change semantics. |
| `_elementtree`, `Modules/_elementtree.c` | `Lib/xml/etree/ElementTree.py` | Element/tree algorithms; XMLParser still imports expat. No pure-Python replacement for the XML tokenizer is supplied here. |
| `_functools`, `Modules/_functoolsmodule.c` | `Lib/functools.py` | Python `cmp_to_key`, `partial`/Placeholder, `_lru_cache_wrapper`, `reduce`; lock primitive needed by cache. |
| `_heapq`, `Modules/_heapqmodule.c` | `Lib/heapq.py` | Python heap operations including the max-heap functions present in 3.14. |
| `_io`, `Modules/_io/` | `Lib/_pyio.py` | Python raw-file, buffered and text classes exist, **2,750** lines. **Not a drop-in bootstrap without `_io`**: it imports `io`, whose public classes come from `_io`; rewire that cycle and retain fd, console, codec and lock primitives. |
| `_json`, `Modules/_json.c` | `Lib/json/{scanner,decoder,encoder}.py` | `py_make_scanner`, `py_scanstring`, Python string encoders and iterencoder. C acceleration covers both scanning and encoding. |
| `_operator`, `Modules/_operator.c` | `Lib/operator.py` | Public operators and getter/caller helpers; **not** native-only private `_compare_digest`. |
| `_pickle`, `Modules/_pickle.c` | `Lib/pickle.py`: `_Pickler`, `_Unpickler`, `_dump[s]`, `_load[s]` | Protocol algorithms exist. `PickleBuffer` is optionally imported from `_pickle`, not implemented there in Python; supply its buffer/lifetime semantics separately. |
| `_queue`, `Modules/_queuemodule.c` | `Lib/queue.py`: `_PySimpleQueue` and Python `Empty` fallback | Python algorithm depends on `threading`/semaphores and `collections.deque`; synchronization and reentrancy properties need review. |
| `_stat`, `Modules/_stat.c` | `Lib/stat.py` | Bit tests/constants; platform-specific constants still need platform data. |
| `_statistics`, `Modules/_statisticsmodule.c` | `Lib/statistics.py`: `_normal_dist_inv_cdf` | One accelerated function, not a C implementation of all statistics. |
| `_zoneinfo`, `Modules/_zoneinfo.c` | `Lib/zoneinfo/_zoneinfo.py`, selected by package `__init__.py` | ZoneInfo algorithm; timezone data discovery and binary reads still need OS/struct services. |
| `_warnings`, `Python/_warnings.c` | `Lib/_py_warnings.py`, used by `Lib/warnings.py` | Python filter/context/reporting logic; C-origin warnings, stack/frame access and startup behavior require VM integration. |
| `_collections`, `Modules/_collectionsmodule.c` | **Partial:** `Lib/collections/__init__.py` | Python `OrderedDict`, `_count_elements`, `_tuplegetter` exist. **No Python `deque`, `defaultdict` or `_deque_iterator`** here: failed imports simply `pass`. Counter/ChainMap/User* are Python already. |
| `_hmac`, `Modules/hmacmodule.c` | **Algorithm fallback:** `Lib/hmac.py` | Python HMAC composition and `_compute_digest_fallback` exist; underlying hashes and constant-time comparison remain native. This is not a replacement for every private `_hmac` object. |
| `_lsprof`, `Modules/_lsprof.c` | **Related implementation:** `Lib/profile.py` versus `Lib/cProfile.py` | Python profiler is available, but not an API/internal-statistics twin of `_lsprof`; event/timing hooks remain native. |

Two important reuse cases are **not C-module twins**. `Lib/_strptime.py` (**822** lines) is already called by C `time.strptime` and date/time `strptime` methods (`Modules/timemodule.c:time_strptime`, `_datetimemodule.c:*_strptime`); it does not replace the clock API. `Lib/_pylong.py` (**729** lines) supplies selected large-int conversion/division algorithms used by `Objects/longobject.c`; it is not an implementation of `int`, and imports `decimal` and `re`, so cannot bootstrap basic integer arithmetic unchanged.

### Algorithm modules without a complete CPython Python twin

| Module(s) | Source | What is missing / proposed treatment |
|---|---|---|
| `_sre` | `Modules/_sre/{sre.c,sre_lib.h}` | `Lib/re/{_parser,_compiler,_constants,_casefix}.py` already parse/compile; matching remains native. Port matching/backtracking, Pattern/Match, replacement and iterator semantics to Python. Preserve backreferences, lookarounds, Unicode rules and errors; substituting a different regex language is not compatibility. |
| `itertools` | `Modules/itertoolsmodule.c` | No importable Python twin. Documentation recipes are useful specifications, not complete replacements for tee sharing, pickle/state, argument/error and iterator-consumption behavior. Implement in Python. |
| `array` | `Modules/arraymodule.c` | No Python twin. Implement sequence policy in Python over ABI-described typed storage/buffer primitives. |
| `_struct` | `Modules/_struct.c`; `Lib/struct.py` | `struct.py` reexports C; no packing/unpacking algorithm. Implement format parsing/loops in Python; primitive ABI size/alignment, endian loads/stores and float representation access. |
| `_random` | `Modules/_randommodule.c`; `Lib/random.py` | `random.py` subclasses `_random.Random`; it does not implement the MT19937 core. Implement deterministic core/state/seeding in Python; retain native OS entropy for seed/SystemRandom/secrets. |
| `math`, `cmath` | `Modules/{mathmodule,cmathmodule}.c` | No complete twin. Keep libm scalar calls/IEEE operations native; write integer combinatorics, iteration and numeric-policy code in Python. Complex special cases still need a deliberate port. |
| `_csv` | `Modules/_csv.c`; `Lib/csv.py` | Python dialect, Sniffer and Dict* wrappers exist; reader/writer state machines do not. Port those algorithms. |
| `binascii` | `Modules/binascii.c` | No twin. `Lib/base64.py` delegates to it. Python encoders/decoders are feasible; CRC/checksum acceleration optional, not a necessary syscall primitive. |
| `unicodedata` | `Modules/unicodedata.c`, generated database headers | No twin. Reuse versioned Unicode data; implement normalization/lookups in Python over compact tables. Count data separately from handwritten logic. |
| `_codecs`; `_multibytecodec`; `_codecs_cn`, `_codecs_hk`, `_codecs_iso2022`, `_codecs_jp`, `_codecs_kr`, `_codecs_tw` | `Modules/_codecsmodule.c`, `Modules/cjkcodecs/`; `Lib/codecs.py`, `Lib/encodings/*` | Registry/error plumbing and codec cores lack complete twins. Many Python encoding modules are wrappers around these. Implement codec algorithms/table use in Python; bootstrap UTF-8/filesystem encoding deliberately. |
| `_tokenize` (core) | `Python/Python-tokenize.c`, `Parser/lexer/`; `Lib/tokenize.py` | 3.14 `tokenize.py` imports `_tokenize.TokenizerIter` unconditionally. No standalone Python tokenizer twin in this checkout. Its Python encoding/untokenize helpers are reusable. |
| `marshal` (core) | `Python/marshal.c` | No production Python twin in CPython; a Python serializer is possible, but code objects/import/bootstrap need runtime cooperation. Do not confuse marshal format with pickle. |

### Native interfaces and runtime modules, also without complete Python twins

Each row is an explicit “no complete CPython Python twin” classification. Python facade modules are listed to avoid mistaking them for replacement implementations. The native division is developed in §4.

| Module(s) | C source / Python facade evidence |
|---|---|
| `_ssl`, `_hashlib` | `Modules/_ssl.c`, `_hashopenssl.c`; `Lib/{ssl,hashlib}.py` wrap native engines. |
| `_md5`, `_sha1`, `_sha2`, `_sha3`, `_blake2` | `Modules/{md5module,sha1module,sha2module,sha3module,blake2module}.c`, `Modules/_hacl/`; native crypto fallback, **not Python fallback**. |
| `zlib`, `_bz2`, `_lzma`, `_zstd` | `Modules/zlibmodule.c`, `_bz2module.c`, `_lzmamodule.c`, `_zstd/`; `Lib/{gzip,bz2,lzma}.py`, `Lib/compression/zstd/` contain higher-level policy. 3.14 zstd must not be omitted. |
| `_sqlite3`, `_dbm`, `_gdbm` | `Modules/_sqlite/`, `_dbmmodule.c`, `_gdbmmodule.c`; `Lib/sqlite3/`, `Lib/dbm/`. `dbm.dumb` is a different storage format/backend, not a dbm/gdbm engine twin. |
| `_ctypes` | `Modules/_ctypes/`; `Lib/ctypes/` delegates calls, memory and callbacks to the extension/libffi. |
| `_socket`, `select` | `Modules/{socketmodule,selectmodule}.c`; `Lib/socket.py`, `Lib/selectors.py`. |
| `posix` / Windows `nt` | `Modules/posixmodule.c`; `Lib/os.py` chooses a native platform module. `nt` is the platform alternative, not another POSIX recipe. |
| `fcntl`, `grp`, `pwd`, `resource`, `syslog`, `termios`, `mmap` | Corresponding `Modules/{fcntlmodule,grpmodule,pwdmodule,resource,syslogmodule,termios,mmapmodule}.c`. |
| `time`, `_signal`, `_locale` | `Modules/{timemodule,signalmodule,_localemodule}.c`; Python date parsing is only a subset of time functionality. |
| `_posixsubprocess`, `_posixshmem`, `_multiprocessing` | `Modules/_posixsubprocess.c`, `_multiprocessing/`; `Lib/subprocess.py`, `Lib/multiprocessing/` retain Python process policy. |
| `_uuid`, `_scproxy` | `Modules/_uuidmodule.c`, `_scproxy.c`; `Lib/uuid.py`, `Lib/urllib/request.py`. UUID has Python algorithms but no twin of the platform UUID wrapper. |
| `readline`, `_curses`, `_curses_panel`, `_tkinter` | `Modules/readline.c`, `_cursesmodule.c`, `_curses_panel.c`, `_tkinter.c`, `tkappinit.c`; `Lib/curses/`, `Lib/tkinter/` are facades over libraries. |
| `pyexpat` | `Modules/pyexpat.c`, `Modules/expat/`; `Lib/xml/parsers/expat.py` reexports it. |
| `_thread`, `_weakref`, `atexit` | `Modules/_threadmodule.c`, `_weakref.c`, `atexitmodule.c`; `Lib/{threading,weakref}.py` supply policy, not scheduler/GC hooks. atexit list policy can move to Python, shutdown entry remains VM-owned. |
| `errno`, `_sysconfig`, `_stat`'s platform additions | `Modules/errnomodule.c`, `_sysconfig.c`, `_stat.c`; platform constants/configuration, not large algorithms. `_stat`'s portable Python fallback is classified above. |
| `_opcode`, `_symtable`, `_types`, `_typing` | `Modules/_opcode.c`, `symtablemodule.c`, `_typesmodule.c`, `_typingmodule.c`; `Lib/{opcode,symtable,types,typing}.py` are not full twins of VM metadata/runtime type implementations. Much typing machinery lives in `Objects/typevarobject.c`, not the small `_typing` module. |
| `_interpreters`, `_interpchannels`, `_interpqueues` | Corresponding `Modules/*module.c`; `Lib/concurrent/interpreters/` provides policy over interpreter isolation/transport. Lightweight processes do not by themselves implement isolated Python interpreters. |
| `_tracemalloc`, `faulthandler`, `_remote_debugging`, `_suggestions` | `Modules/_tracemalloc.c`, `faulthandler.c`, `_remote_debugging_module.c`, `_suggestions.c`; `Python/{tracemalloc,remote_debugging,suggestions}.c` provide substantial backing logic. |
| `builtins`, `sys`, `_imp`, `_ast`, `gc`, `_contextvars`, `_string` (extra core entries) | `Modules/config.c.in`: `Python/{bltinmodule,sysmodule,import,Python-ast,context,_contextvars}.c`, `Modules/gcmodule.c`, `Objects/unicodeobject.c`. `Lib/{ast,contextvars,string}.py` are not complete twins. These expose language/runtime state; move public algorithms to Python while preserving native execution/GC/reflection hooks. |
| Windows `_winapi`, `_overlapped`, `msvcrt`, `winreg`, `winsound`, `_wmi` | `Modules/_winapi.c` (**3,240** lines), `Modules/overlapped.c` (**2,093**), `PC/msvcrtmodule.c` (**674**), `PC/winreg.c` (**2,239**), `PC/winsound.c` (**276**), `PC/_wmimodule.cpp` (**391**, C++); `PC/config.c`, `PCbuild/pcbuild.proj` and extension projects. No twins of the native interfaces. `Lib/platform.py` can gather some information without `_wmi` but is not a WMI implementation. `PC/` and C++ are outside the requested C census. |

Test/example modules are not stdlib accelerators and have no production Python twins: `_testbuffer`, `_testcapi`, `_testinternalcapi`, `_testlimitedcapi`, `_testclinic`, `_testclinic_limited`, `_testimportmultiple`, `_testmultiphase`, `_testsinglephase`, `_ctypes_test`, `_xxtestfuzz`, `xxsubtype`, `xxlimited`, `xxlimited_35` are named in `Modules/Setup.stdlib.in`; `Modules/xxmodule.c` is a template. Their sources remain in the physical census. Core `_warnings`, `_string`, `_contextvars`, `gc`, `_ast`, `_tokenize`, `marshal`, `_imp`, `builtins`, `sys` are not omitted just because they lack Setup recipes (`Modules/config.c.in`).

## 3. What PyPy actually puts at each level

All PyPy paths in this section are relative to **`vendor/pypy3`**, the pinned `py3.12` branch. Its `pypy/module/sys/version.py` sets `CPYTHON_VERSION = (3, 12, 15, ...)`; it is Python 3 evidence, not a ready-made 3.14 standard library. PyPy distinguishes interpreter-level objects/`space` operations from ordinary application Python; RPython is translated implementation code, not ordinary Python methods executing in the guest VM. This distinction is documented in the [PyPy glossary](https://doc.pypy.org/glossary.html) and [architecture overview](https://doc.pypy.org/architecture.html), and directly visible in `pypy/objspace/std/objspace.py`, `pypy/interpreter/gateway.py`, and the module definitions below.

| Measured source partition | Files | Physical lines | Interpretation |
|---|---:|---:|---|
| `pypy/objspace/std/*.py`, immediate children | 39 | 27,640 | Implementation-side built-in object code, predominantly RPython; includes initialization/helpers and embedded app-level strings. |
| `pypy/module/**/*.py`, excluding named test/benchmark paths | 501 | 115,654 | All module implementation/configuration sides combined. |
| Of those, `app_*.py` | 34 | 2,553 | Explicit app-level file subset; not all app-level code. |
| Remaining module files | 467 | 113,101 | Interpreter-side **source partition**, not a claim that every line is translatable RPython. `moduledef.py`, setup code, generated declarations and embedded strings are included. |
| `lib_pypy/**/*.py`, same test-path exclusion | 203 | 58,853 | App-facing replacements **plus** CFFI, HPy support, build descriptions, bundled parser/data code and a few test-helper names. |
| Of those, bundled `lib_pypy/cffi/` | 28 | 15,415 | Dependency implementation, not 15K lines of stdlib algorithms to reuse. |
| Of those, `*_build.py` | 20 | 3,809 | Build descriptions; include embedded C and test-extension builders, never executed here. |

Counts use the pathname filters printed in the script. The main module/lib census excludes directory components `test`, `tests`, `testing`, `bench`, `benchmarks`, and any pathname component beginning `test_`, `apptest_`, or `conftest.py`; this also removes the `test_lib_pypy` and `test_pypy_c` subtrees. The explicit app-file subset excludes the named test/benchmark directories. This is a reproducible source partition, not a semantic classifier; names such as `_testcapi.py` remain visible. Explicit app files are a lower bound because `gateway.applevel(...)` embeds Python inside implementation files, including `objspace/std/{objectobject,dictmultiobject,typeobject,setobject}.py`. Per-module counts and all **71 `moduledef.py` paths** (**70 top-level module directories plus `_rawffi/alt`**) are in Appendix A.

### Built-in types and interpreter-level modules

`pypy/objspace/std/objspace.py:builtin_type_classes` registers **18 entries**, including a sequence-iterator type: bool, bytearray, bytes, complex, dict, float, int, list, memoryview, NoneType, object, set, frozenset, slice, tuple, type, str and the iterator. Implementations are in `boolobject.py`, `bytearrayobject.py`, `bytesobject.py`, `complexobject.py`, `dictmultiobject.py`, `floatobject.py`, `intobject.py`, `listobject.py`, `longobject.py`, `memoryobject.py`, `noneobject.py`, `objectobject.py`, `setobject.py`, `sliceobject.py`, `tupleobject.py`, `typeobject.py`, `unicodeobject.py`, `iterobject.py`; strategy helpers include `mapdict.py`, `kwargsdict.py`, `specialisedtupleobject.py`, `smalllongobject.py`. `longobject.py` implements large values of Python 3 `int`, not a separate public `long` type. **PyPy does not demonstrate Python-level dict/list builtins over a tiny guest primitive set.** It demonstrates high-level implementation code translated with its interpreter.

This is the complete module-directory inventory, not a claim that all are enabled in every PyPy build or all define interpreter-level exports (`pypy/module/*/moduledef.py`, Appendix A). Most mix interpreter-level exports with optional app-level exports; `atexit` is app-level, while `zipimport` installs the frozen Python module from `lib-python/3/zipimport.py`:

| Group | Directories |
|---|---|
| Runtime/language | `__builtin__` (exports `builtins`), `__pypy__`, `_abc`, `_ast`, `_codecs`, `_continuation`, `_frozen_importlib`, `_io`, `_locale`, `_opcode`, `_pickle_support`, `_string`, `_warnings`, `_weakref`, `atexit`, `exceptions`, `gc`, `imp`, `marshal`, `operator`, `pwd`, `signal`, `sys`, `thread` (exports `_thread`), `time`, `token`, `unicodedata`, `zipimport` |
| Algorithms/data | `_collections`, `_csv`, `_md5`, `_multibytecodec`, `_pickle`, `_pypyjson`, `_random`, `_sre`, `array`, `binascii`, `cmath`, `itertools`, `math`, `micronumpy`, `struct` |
| OS/libraries | `_minimal_curses`, `_multiprocessing`, `_posixsubprocess`, `_socket`, `_winreg`, `bz2`, `crypt`, `errno`, `fcntl`, `mmap`, `posix`, `pyexpat`, `select`, `termios`, `zlib` |
| FFI/tooling | `_cffi_backend`, `_cppyy`, `_demo`, `_hpy_universal`, `_jitlog`, `_lsprof`, `_rawffi` (also nested `alt`), `_testing`, `_vmprof`, `cpyext`, `faulthandler`, `pypyjit` |

Examples inspected: `itertools/moduledef.py` exports its iterator classes entirely from `interp_itertools`; `_collections/moduledef.py` exports RPython deque and app-level defaultdict/OrderedDict, with an interpreter helper for defaultdict; `_io/moduledef.py` exports RPython I/O classes and an app-level text-encoding helper; `struct/moduledef.py` exports `_struct` from `interp_struct`, despite a stale comment about an older pure-Python file; `_sre/interp_sre.py` delegates to `rpython/rlib/rsre/`; `_random/interp_random.py` to `rpython/rlib/rrandom.py`; `zlib/interp_zlib.py` to `rpython/rlib/rzlib.py`; `bz2/interp_bz2.py` uses `rffi`. Counting only `pypy/module` misses those underlying algorithm/library layers. Also, **18,003** implementation-side lines of `cpyext`, **7,193** of `_hpy_universal`, and **16,111** lines of `micronumpy` (including its app file) must not be billed as generic stdlib reuse. HPy source remains present even though the standard module configuration leaves it disabled (`pypy/config/pypyoption.py`).

### App-level Python and CFFI

The **34 app files** cover `__builtin__/app_{breakpoint,functional,inspect,io,operation}.py`, `__pypy__/app_{debug,signal}.py`, `_abc/app_abc.py`, `_collections/app_{defaultdict,odict}.py`, `_continuation/app_continuation.py`, `_csv/app_csv.py`, `_demo/app_demo.py`, `_io/app_io.py`, `_minimal_curses/app_curses.py`, `_multibytecodec/app_multibytecodec.py`, `_pickle/app_pickle.py`, `_rawffi/alt/app_struct.py`, `_sre/app_sre.py`, `_testing/app_notrpython.py`, `_vmprof/app_vmprof.py`, `_warnings/app_warnings.py`, `_weakref/app_weakref.py`, `atexit/app_atexit.py`, `exceptions/app_group.py`, `fcntl/app_fcntl.py`, `gc/app_referents.py`, `math/app_math.py`, `micronumpy/app_numpy.py`, `operator/app_operator.py`, `posix/app_{posix,startfile}.py`, `pwd/app_pwd.py`, and `time/app_time.py`. Exports include any/all/sum/sorted, vars/dir, input/print, breakpoint, bin/oct/hex, aiter/anext, ABC policy, exception groups and several math algorithms (factorial, remainder, isqrt, prod, sumprod, comb, perm, lcm). Source of actual exports is each directory's `moduledef.py`: public range/map/filter/zip are interpreter-level classes in `__builtin__/functional.py`, despite an unused app-level filter implementation. This is useful precedent for moving policy and loops above primitives; Python 3.14 differences still need review.

Useful `lib_pypy/` examples include `_contextvars.py` over VM context hooks and `_immutables_map.py`, `_structseq.py`, `_marshal.py`/`marshal.py`, `_ctypes/`, standalone hash implementations, and pyrepl. Python datetime now comes from copied CPython `lib-python/3/{datetime,_pydatetime}.py`, outside the `lib_pypy` count. Important counterexamples: `_collections.py` labels its deque implementation documentation-only and not thread-safe; `_codecs_cn.py` and siblings redirect to native `_multibytecodec`; `readline.py` says its API is a subset with stubs. Being a `.py` file does not establish independent implementation or full compatibility (the named files' imports/docstrings).

Source-visible CFFI use covers **19 named module surfaces**, plus an optional decimal binding and a small Darwin ctypes support path. This counts available wrapper implementations, not modules enabled on every platform/build:

| Surfaces | App-level wrappers / binding evidence in `lib_pypy/` | Actual native work |
|---|---|---|
| `_ssl`, `_hashlib` | `_ssl/__init__.py`, `_cffi_ssl/_stdssl/`, `_hashlib/__init__.py`, `_ssl_build.py` | OpenSSL via `_pypy_openssl`. |
| `_sqlite3`, `_gdbm` | `_sqlite3.py`, `_sqlite3_build.py`; `_gdbm.py`, `_gdbm_build.py` | sqlite3 and gdbm. |
| `_curses`, `_curses_panel` | `_curses.py`, `_curses_panel.py`, `_curses_build.py` | curses/panel. |
| `syslog`, `resource`, `grp` | Corresponding Python wrappers; `_syslog_build.py`, `_resource_build.py`, `_pwdgrp_build.py` | libc/system calls; `grp.py` imports `_pwdgrp_cffi`. `pwd` itself is an interpreter module here. |
| `_tkinter` | `_tkinter/{app,tclobj}.py`, `_tkinter/tklib_build.py` | Tcl/Tk. |
| `_crypt`, `audioop` | `_crypt/__init__.py` uses `ffi.dlopen('crypt')`; `audioop.py`, `_audioop_build.py` | libcrypt; embedded native audio algorithms. Retained in this 3.12 tree but removed from CPython 3.14. An RPython `_crypt` alternative also exists in `pypy/module/crypt/`. |
| `msvcrt`, `_winapi`, `_overlapped` | Corresponding `.py` wrappers; `_pypy_winbase_{build,cffi,cffi64}.py` | Windows CRT/kernel32, Winsock and asynchronous I/O through CFFI. |
| `_lzma`, `_posixshmem` | `_lzma.py`, `_lzma_build.py`; `_posixshmem.py`, `_posixshmem_build.py` | liblzma; POSIX shared memory. |
| `_blake2`, `_sha3` | Package `__init__.py` and respective `_blake2_build.py`, `_sha3_build.py` | Bundled native BLAKE2 and Keccak implementations. |
| Optional `__decimal` | `__decimal.py`, `_decimal_build.py` | libmpdec through `_decimal_cffi`; the builder is commented out in `pypy_tools/build_cffi_imports.py`. Its default usability is **unverified**. |
| Extra `_ctypes` Darwin shim | `_ctypes/__init__.py`, `_ctypes/_ctypes_build.py` | dyld support. The main `_ctypes` implementation uses native `_rawffi`, **not entirely CFFI**. |

The **20 `*_build.py` files** include `_pypy_util_build.py` support, shared binding builders and test-extension builders; they do not map one-to-one to the 19 surfaces. `_dbm.py` and `_scproxy.py` use **ctypes**, and `zlib`, `bz2`, `pyexpat`, sockets and libm use **RPython/rffi** paths: do not label every external binding CFFI. All counts refer only to this checked-in branch; a Python-3.14-ready PyPy port is **unverified**.

## 4. What remains native, and at which boundary

**Proposal — unverified:** distinguish three budgets: VM primitives, thin external-library/syscall adapters, and native algorithm implementations retained from dependencies. A primitive `inflate_step` is small at the boundary while zlib behind it is a substantial native algorithm. “Mostly Python stdlib” should describe owned language/policy/algorithm code and count external libraries separately, not hide them in one primitive. Existing CPython facade/engine boundaries below support the division, not its unmeasured performance.

| Area | Native primitive / adapter | Native algorithm or runtime substrate retained | Python responsibilities and concurrency boundary |
|---|---|---|---|
| `ssl`/`hashlib`/`hmac` | Opaque context create/update/final/copy/free, TLS handshake/read/write, certificate/provider operations, content-independent comparison, OS entropy | OpenSSL TLS/crypto and HACL* fallback implementations | Python API/policy, stream integration, validation and exceptions. Keep secret-dependent crypto outside speculative JIT rewrites; opaque contexts need ownership/locks and pinned inputs. Python HMAC composition exists but does not replace vetted hashes/comparison. |
| `zlib`/`bz2`/`lzma`/**zstd** | Stateful streaming calls with consumed/produced lengths and explicit limits | zlib, libbz2, liblzma, libzstd | Python file/format wrappers, buffering, concatenated-stream policy and resource limits; serialize mutation of each native stream, allow independent contexts in parallel. Sources: corresponding `Modules/` and `Lib/` files in §2. |
| `sqlite3` | Connection/statement/step/bind/column/finalize/blob calls; registered callback trampolines | SQLite SQL compiler, storage, transactions/locking | Python Connection/Cursor/Row policy may move above binding. Preserve `check_same_thread`, SQLite build threading mode, callback reentry and transaction/error behavior; “no GIL” does not mean arbitrary simultaneous use of one connection is valid. `Modules/_sqlite/{connection,module,cursor}.c`. |
| `_ctypes`/FFI | Load/symbol, ABI call, callback closure, owned native memory, buffer pinning | libffi ABI lowering, platform loader and native trampolines | Python type descriptors/conversion/pointer policy. Preserve calling conventions, varargs, errno/last-error and callbacks arriving from foreign threads; arbitrary user C cannot be assumed scheduler-aware. `Modules/_ctypes/{callproc,callbacks,cfield}.c`. |
| `os`/`select`/`socket` | fd/handle syscalls, poll/epoll/kqueue/IOCP, DNS/NSS resolver calls, subprocess exec/spawn support | Kernel and system resolver; short native post-fork child path where only async-signal-safe work is valid | Python path/selectors/socket/subprocess policy, timeout/retry/error translation. Park lightweight processes on nonblocking operations; offload genuinely blocking foreign calls. Don't run arbitrary Python after fork in a formerly multithreaded child. `Modules/{posixmodule,selectmodule,socketmodule,_posixsubprocess}.c`. |
| `time` | Clock read, kernel timer/wait, platform timezone/strftime glue | OS clocks/timers and libc locale/timezone mechanisms | Calendar arithmetic, `_strptime`, higher-level sleep/deadline policy. Native wait integrates scheduler; realtime and monotonic clocks remain distinct. `Modules/timemodule.c`, `Lib/{_strptime,_pydatetime}.py`. |
| `signal` | Minimal signal-safe handler, pending flags and wakeup fd; disposition/mask calls | Asynchronous delivery mechanism; VM polling/interrupt hooks | Python handler dispatch at safe points in the designated Python main thread; M:N migration must not accidentally change that contract. `Modules/signalmodule.c:trip_signal`, `Lib/signal.py`. |
| libm/float conversion | IEEE arithmetic/bit operations, scalar libm calls; optionally proven parse/shortest-format kernels | Platform libm; selected validated numeric kernels if retained | Python numeric policy, formatting grammar and integer algorithms. Correct rounding/NaN/signed-zero behavior is a porting obligation; not everything in `mathmodule.c` is a single libc call. `Modules/{mathmodule,cmathmodule}.c`, `Python/{dtoa,pystrtod}.c`. |
| XML/UI/dbm/locale | Handle-based library calls | Expat, Tcl/Tk, curses/readline, dbm/gdbm, OS locale | Python tree/UI/database wrappers and policy. These are deliberate reuse decisions, not mathematical necessities for native execution. Sources: §2 interface inventory. |
| VM/GC/C ABI | Allocation, roots/barriers, atomic memory, call/exception/frame transitions, buffer and C-API entry/lifetime support | Collector, scheduler, code generator/deoptimization, foreign-thread attachment, ABI-compatible object headers | Python builtins and runtime policy use these primitives; Python-source algorithms do not remove these obligations. `Include/cpython/*.h`, `Python/{gc,critical_section,qsbr}.c`, `Objects/*object.c`. |

The reasons differ. Syscalls, asynchronous signal entry and foreign ABI transitions require a platform/runtime boundary. **Recommendation — unverified implementation cost/performance:** retain compression, SQLite and XML engines to reuse their format, streaming, durable-storage and error-handling implementations, rather than undertake separate engine replacements alongside Python compatibility. Python can in principle implement those algorithms; retaining native libraries is an engineering choice, and their speed under Lucifer is unmeasured. The binding/library split is verified in the `Modules/` sources named in the table.

Security rationale is specific. CPython's `Modules/_hacl/README.md` describes HACL*'s verification properties; `Modules/hmacmodule.c` uses HACL* and `_hashopenssl.c` uses OpenSSL. [hashlib documentation](https://docs.python.org/3.14/library/hashlib.html) records verified HACL* fallback for selected unavailable OpenSSL hashes. [OpenSSL's CRYPTO_memcmp contract](https://docs.openssl.org/3.0/man3/CRYPTO_memcmp/) says work depends on length, not compared contents; [hmac documentation](https://docs.python.org/3.14/library/hmac.html) explains compare_digest's scope and length/type caveat. **Native code alone is not a constant-time proof**, nor is all of OpenSSL universally constant-time. Retaining established implementations reduces the new security argument; preserving relevant compiler/CPU assumptions and reviewing each wrapper remains necessary. A Python comparison loop optimized by an ordinary JIT has no verified constant-time guarantee here.

CPython 3.14 already declares `_ssl`, `_hashlib`, `_sqlite3`, `_ctypes`, zlib, bz2 and lzma as `Py_MOD_GIL_NOT_USED` in their module slots (their `Modules/` source). This is evidence worth studying for per-context synchronization; it does **not** prove that copying their wrappers into a different scheduler/GC/ABI is correct. Lucifer must preserve buffer lifetime, release/reacquire the right object locks across callbacks, attach foreign threads, and translate errors on the correct Python thread state. No global lock is proposed as a correctness escape hatch.

## 5. Proposed primitive contract and Python-owned builtins

**Proposal — unverified:** assign stable semantic IDs, as Smalltalk does, but group operations by contracts rather than inventing one primitive per Python method. The ID ranges below are reservations, **not measured primitive counts**. A descriptor records argument/result kinds, allocation/exception behavior, memory effects, safepoint/reentry/park capability, and target ABI. JIT intrinsics lower these same operations; interpreter/template execution and C entry use the same semantics. Guard invalidation/deoptimization cannot skip publication, exception or ownership effects. Source precedent: Smalltalk `docs/vm/source-primitive-DESIGN.md` stores semantic primitive IDs, distinguishes success/fallback/park/transfer, and avoids persisting native handler addresses.

| Proposed IDs | Minimal operations / contract | Algorithms deliberately left in Python |
|---|---|---|
| 1–31: objects/types | Allocate zeroed fixed/variable-size object of a trusted layout; identity/type; checked private field load/store; publish; GC barrier/root handle; bootstrap/finalize built-in type and slot descriptors | Constructors, method bodies, attribute/descriptor policy where possible. Raw field access stays private to trusted core code, not an application escape hatch. |
| 32–95: scalar values | Tagged integer add/sub/mul/divmod/shift/bit ops/compare with explicit overflow-to-bignum path; float arithmetic, conversion, compare, reinterpret bits; word multiply/carry and digit-span access for bignums | Bignum loop structure/normalization/Karatsuba/division and int parsing/formatting in Python; optional large-number kernels only after evidence. An opaque native whole-bignum engine would be a separately counted algorithm dependency. |
| 96–159: storage | Allocate/resize owned reference slots, bytes and 1/2/4-byte character storage; bounds-checked read/write; copy/move/fill/compare; typed endian loads/stores; pinned buffer lease/export; memoryview shape/stride metadata | list/tuple/dict/set/str/bytes/bytearray/array storage policy, searching/slicing/encoding/formatting. Bulk copy primitives must apply reference barriers/refcount rules, not just memcpy object pointers. |
| 160–223: control/state | Call/return/raise, frame and traceback access, type-slot call adapters, weak references/finalization, context attach/detach and context-variable switching, C handle/ref transitions | Builtin function algorithms, descriptors and context-manager policy. Bootstrap uses pre-created code/type/exception objects rather than requiring a Python dict to create its own first dictionary. |
| 224–287: concurrency | Object lock/version, atomic load/store/CAS, acquire/release publication, wait/wake/park, cancellation/signal polling, foreign-thread attach, native-call blocking transition | Container critical-region algorithms, queue/threading policy and iterator mutation checks. No hidden global serialization. |
| 288–511: platform adapters | Typed fd/handle/filesystem/process/network/time/entropy/locale operations; capability/data constants; dynamic loader/libffi entry | os/socket/selectors/subprocess/time/ctypes policy. Several hundred platform operations may be needed; a generic syscall(number, ...) does not erase their platform semantics. |
| 512+: library adapters | Explicit typed opaque-handle families for crypto, compression, SQL, XML, UI and dbm | Facade classes, conversion, buffering, resource policy. Audit/count dependencies separately; unrestricted generic C calls are not the entire public primitive design. |

IDs must have a versioned manifest; allocation failure, arbitrary Python callbacks, suspension and partial writes are explicit outcomes. Type/shape guards must check the ABI representation currently in use. This proposed contract needs experiments; it is not evidence of a particular primitive total or JIT speed.

| Python-owned family | Concrete scope | Source basis / remaining dependency |
|---|---|---|
| `dict`, `set`, `frozenset`, `list`, `tuple` | Hash probing/resizing/order, sequence growth/slicing, equality, iteration, sorting/Timsort and mutation checks | Port semantics from `Objects/{dictobject,setobject,listobject,tupleobject}.c`, storage strategy ideas from PyPy `objspace/std/`. Private raw slots avoid circular calls to public list/dict methods. C-visible geometry constrains strategies (§7). |
| `str`, `bytes`, `bytearray`, `memoryview`, `array` | Unicode/string algorithms, join/split/search/replace, repr/format, normalization, encoding, typed sequence policy | `Objects/{unicodeobject,bytesobject,bytearrayobject,memoryobject}.c`, `Objects/stringlib/`, `Modules/{arraymodule,unicodedata}.c`; retain versioned data and native buffer leasing. |
| `int`, `bool`, `float`, `complex` | Python methods, arbitrary-precision algorithms, hashing and formatting/parse grammar | Word/IEEE operations intrinsic; bootstrap numeric formatting cannot import the full decimal/re stack through `_pylong` recursively. Sources: `Objects/{longobject,floatobject,complexobject}.c`, `Lib/_pylong.py`. |
| object/type/descriptor/exception policy | Public attribute/constructor/MRO/subclass/descriptor behaviors, exception methods and repr | VM owns type creation, slot/code transitions and layout validation. `Objects/{typeobject,object,descrobject,exceptions}.c`; `Python/bltinmodule.c`. Not an ordinary mutable Python class with an extra `__dict__`. |
| Builtin functions and iterator types | enumerate/range/reversed/map/filter/zip, any/all/min/max/sum/sorted/print, property/staticmethod/classmethod/super | Python implementations over call/frame/storage primitives; 3.14 numeric accumulation, strict zip/map behavior, errors and pickling need exact treatment. Sources: `Python/bltinmodule.c`, `Objects/{rangeobject,enumobject,iterobject}.c`; PyPy app examples target 3.12 and still need 3.14 semantic review. |
| Existing twins | Decimal, datetime, I/O, pickle, JSON, ABCs, functools, bisect/heapq, zoneinfo, warnings, asyncio | Reuse §2 sources; fix bootstrap, buffers, capsulated C APIs and synchronization. |
| Missing portable algorithms | Regex matcher, itertools, deque/defaultdict, CSV, struct, MT19937, binascii, codecs, marshal and tokenizer | New/adapted Python ports; CPython 3.14 tests determine observable behavior. Native entropy/libm/ABI/data remain primitives. |
| Rest of `Lib/` | pathlib, importlib, asyncio policy, networking protocols, logging, packaging utilities, HTTP, unittest, etc. | Start from the measured **718 files / 329,050 lines**, including wrappers and data; preserve tests separately. Imports of absent private native modules must be redirected, not assumed to work. |

Concurrent containers are a core implementation task, not a decorator to apply later. For example, dict `__hash__`/`__eq__`, list comparisons and sort keys may call Python that mutates the same container. Use per-object ownership/version/retry rules, release or safely suspend locks around callbacks, and revalidate structure before using stale storage. JIT loads must not outlive their publication/version guards. Reference: CPython `Objects/dictobject.c` and `listobject.c` critical sections, `Include/cpython/critical_section.h`, `Python/critical_section.c`. **Proposal — unverified:** preserve the free-threaded CPython semantics of primitive operations without promising that compound application sequences such as check-then-set become atomic. Shared iterators/native contexts need explicit state protection too; the absence of a GIL is not permission to race on their cursors.

## 6. Python LOC: reuse, adaptation and new work

**Measured:** the script's **23-file candidate reuse cohort is 27,523 physical lines**. It includes `_pydecimal` 6,389, `_pydatetime` 2,734, `_pyio` 2,750, pickle 1,931, functools 1,166, collections 1,605, four JSON files 1,255, `_strptime` 822 and other files listed in Appendix A. Some entries (`tokenize`, `_pylong`) are partial components, not standalone fallbacks. Adding already-Python asyncio Future/Task (**1,622**), hmac (**256**) and profile (**615**) gives a broader **30,016-line** reference cohort. All are **inside** the 329,050-line Lib total, never additive to it. These sizes are auditable starting material, not compatibility-test results.

**Estimate — unverified:** planning ranges below count physical implementation Python, exclude tests/generated Unicode and codec tables, and deliberately do not use a C-to-Python LOC ratio. “Write/adapt” includes substantial semantic ports, whether typed afresh or adapted from CPython/PyPy algorithms; it is separate from carrying forward already-Python library files.

| Work | Estimated Python lines | Basis and confidence |
|---|---:|---|
| Reuse existing CPython Python library | **280K–329K** carried forward | Measured ceiling 329,050. Lower end allows removal of tools/duplicate fallback facades and platform selection; no per-file retain manifest yet, so retained subset is unverified. Existing implementation code here still needs review. |
| Enable/review the Python fallback/core cohort | **~30K existing**, within the reuse row | Measured 30,016 above; not 30K newly written. Startup rewiring and lock/ABI fixes are not measured yet. |
| Builtin container/object/type algorithms | **16K–28K** write/adapt | Reference scale: PyPy 3.12 std object space **27,640** lines before porting implementation-side helpers and reviewing 3.14 semantics; CPython algorithms and C layout obligations determine details. |
| Numeric/string algorithms beyond the existing cohort | **10K–20K** write/adapt | `Objects/{longobject,unicodeobject,floatobject,complexobject}.c`, stringlib, PyPy object sources. Raw primitives/data excluded. |
| Missing algorithm modules | **9K–18K** write/adapt | §2: regex, itertools, CSV, struct, MT, binascii/codecs, deque and serialization/tokenizer gaps. `_sre/sre_lib.h` and PyPy's rlib are extra implementation sources, not free imports. |
| Python facade/FFI/runtime-policy integration | **8K–16K** write/adapt | Verified PyPy ctypes/CFFI wrappers demonstrate the division; 3.14 behavior and ownership require porting. Excludes CFFI toolchain and external engines. |
| Bootstrap/type-slot/ABI/concurrency adaptation in Python | **7K–13K** write/adapt | Required mechanisms in §§5/7/8; no Lucifer implementation exists to count. Does not include native C-API/GC/JIT implementation. |
| **Additional Python implementation work** | **50K–95K** write/adapt | Sum of the five work rows; a planning envelope, not a schedule or a measured lower bound. |

The table therefore suggests roughly **330K–424K total owned implementation Python** after reuse plus additional work, before tests/data; this may shrink with consolidation or grow with omitted platform details. The ranges are not independently validated. The *newly invented versus mechanically ported* split inside 50K–95K is **unverified** and cannot honestly be sharpened from line counts alone. A first dict/list/str/struct vertical slice should replace this estimate with observed port effort while preserving all fixed compatibility requirements.

PyPy is not an additional 58,853 free lines. Removing its bundled CFFI's **15,415** leaves **43,438**; removing the disjoint `*_build.py` subset leaves **39,629** across the rest of `lib_pypy`, still including HPy support, SSL support/data, optional or historical APIs, and redirectors. Many algorithms duplicate CPython's twins. Treat PyPy primarily as semantic/FFI/strategy reference and select small ports with attribution, rather than adding its raw count to the reuse total. This estimate excludes the native runtime, full C-API stubs, platform porting and external libraries; those are substantial separate deliverables (`pypy/module/cpyext/` alone illustrates why).

## 7. The full C-API constrains Python-written builtins

See [01 — C-API and object model](01-capi-object-model.md), being authored independently; this report does not depend on its existence or conclusions.

Writing `class list` in Python does not satisfy C code compiled against `PyListObject`. `Include/cpython/listobject.h` exposes `ob_item` and direct `PyList_GET_ITEM`/`PyList_SET_ITEM`; `dictobject.h` exposes `ma_used`, keys and values; Unicode/long/tuple/float and buffer headers expose other representation contracts. The Python source language of a method is independent of the ABI layout of its receiver. `Modules/_datetimemodule.c` additionally exports `datetime_CAPI`, so choosing `_pydatetime` alone does not satisfy C consumers of datetime.

**Proposal — unverified:** bootstrapped built-in types have fixed compatible headers/storage and generated native C entry/slot trampolines calling JIT-compiled Python methods. A single authoritative object/storage representation is the simplest starting point to investigate. Native inline macros must see current data; maintaining a second shadow list/dict requires coherent identity, mutation, borrowed-reference lifetimes and arbitrary native retention, not merely copying at calls. Storage strategies/unboxed elements are usable only where every possible C observer remains correct. On C-visible lists, `ob_item` is an array of `PyObject*`, not tagged values. The concrete tradeoff and any non-exposed fast representation must be settled by 01's object model, not assumed here.

Tagged ints/floats in JIT registers must materialize valid CPython objects when observable, with identity and lifetime preserved where Python/C require it. Object relocation, pinning, reference counting, finalizer timing, weak references, exception state, vectorcall, buffer exports, type slots, subclass layout and thread attachment remain native runtime/C-API obligations even when every container algorithm is Python. Sources: `Include/{object,datetime}.h`, `Include/cpython/{object,listobject,tupleobject,longintrepr,unicodeobject,floatobject}.h`, `Include/pybuffer.h`, `Objects/*object.c`.

The **unmodified numpy build + no GIL** combination needs an explicit ABI/concurrency contract: exactly which CPython version/build ABI is targeted, whether the supplied numpy binary is free-threaded or conventional, what inline macros compiled into it do, and how unsupported concurrent extension accesses are handled without reintroducing a global lock. This report neither weakens that bar nor declares it solved. Container correctness in Python alone cannot repair an unsafe raw concurrent native write; the object model and extension boundary must provide the argument. Relevant source: `Include/object.h`'s `Py_GIL_DISABLED` branches and the exposed headers above; independent design in 01.

## 8. Startup and snapshots

### CPython `python -c pass` time

No local timing was run: `AGENTS.md` prohibits benchmarks, including a repeated startup loop or launching vendored interpreters. Published primary evidence gives useful scale without pretending it is a local 3.14.0 measurement:

| Published run | `python -c pass` (`python_startup`) | `python -S -c pass` (`python_startup_no_site`) | Scope |
|---|---:|---:|---|
| CPython **3.14.0a7+**, `ea8ec95`, 2025-04-21, `linux-x86_64` runner | **13.2 ms** | **8.20 ms** | Mainline baseline in [faster-cpython's published comparison](https://github.com/faster-cpython/benchmarking-public/blob/4f8f80bf1d1905412375263501921b977c225001/results/bm-20250425-3.14.0a7%2B-9fc1238/bm-20250425-linux-x86_64-mdboom-python_startup_time-3.14.0a7%2B-9fc1238-vs-base.md). Experimental comparison column is 13.3/7.31 ms; not a release result. |
| CPython **3.13.0**, `60403a5`, `pythonperf1-amd64` baseline | **24.4 ms** | **16.9 ms** | Independent runner/version context in [faster-cpython's 2025-10-04 report](https://github.com/faster-cpython/benchmarking-public/blob/main/results/bm-20251004-3.15.0a0-880c952-NOGIL/bm-20251004-pythonperf1-amd64-python-880c9526f91960b9cba5-3.15.0a0-880c952-vs-3.13.0.md). These are the baseline column, not its free-threaded experimental column. |

These are verified **published observations**, not newly replicated measurements; exact release-3.14.0 time on this host and Lucifer startup time are **unverified**. Searches of faster-cpython's published results did not locate a verified exact-3.14.0 release run; the alpha result is explicitly not substituted for one. The benchmark measures process execution through exit, not only import time. Its definition is inspected locally in `vendor/pyperformance/pyperformance/data-files/benchmarks/bm_python_startup/run_benchmark.py`: command is `[sys.executable, '-c', 'pass']`, optionally `-S`. Different machines/builds/environments explain why no universal “CPython takes N ms” is asserted; this table is not a controlled version comparison or a promised target.

### How much Python loads

`Tools/build/freeze_modules.py:FROZEN` names three import-system modules, three startup-without-site modules (`abc`, `codecs`, `io`), and eight startup-with-site entries (including both OS path alternatives). These are **frozen code objects**, not an already-initialized object heap; initialization still has work to do (`Python/frozen.c`, `Python/pylifecycle.c`). A static source envelope is measurable without importing anything:

| Source envelope, counted in Appendix A | Physical Python lines | Limit of inference |
|---|---:|---|
| importlib bootstrap/external, zipimport; codecs; encodings init/aliases/UTF-8/ASCII; io; abc | **6,235** | Ten source files. Both sample encodings are included for coverage; actual platform/locale imports vary. Source lines are not executed-bytecode or memory counts. |
| Additional POSIX/site envelope: `_collections_abc`, os, stat, genericpath, posixpath, site, `_sitebuiltins` | **4,241** | Seven files; excludes alternative ntpath's 914 lines. `.pth`, sitecustomize/usercustomize and installed packages can add arbitrary imports. |
| Both envelopes on POSIX | **10,476** | Static 17-file reference envelope, not a measured transitive startup closure. Including ntpath gives the script's 18-file total **11,390**. |

Lucifer also needs its Python-written builtins before these imports can work. **Proposal/estimate — unverified:** preload an initialized, minimal kernel covering type/object/exceptions, dict/list/tuple/str/bytes/int/float and iterator essentials, plus import/codec/I/O bootstrap; keep re, decimal, datetime, HTTP, tkinter and other optional packages demand-loaded. A **15K–30K-line Python kernel budget** is a hypothesis to check, not measured startup imports; eager kernel reachability, code/data image size and allocation counts must be recorded once it exists. `Lib/_pyio.py`'s import cycle, `_pylong.py`'s decimal/re dependencies, and `_py_abc.py`'s weakrefs show why simply importing every fallback during bootstrap is not a viable construction order.

### Snapshot image design

Verified Smalltalk precedent: `docs/vm/snap-DESIGN.md` describes semantic-state restoration while rebuilding ephemeral runtime state; `vm/snap_wire.inc` currently declares `SNAP_SVER = 61` and logical Process/frame/slot geometry; `docs/vm/source-primitive-DESIGN.md` persists semantic primitive IDs, not native handler addresses. Historical snapshot documents include superseded format details, so use current wire/code as authority. `docs/vm/snapshot-code-INTEGRATION.md` reports warm-filesystem fresh loads **27.558–28.517 ms** for a specific 10K-method fixture with shared 21-byte code bodies. That is evidence that snapshot loading has real cost, **not a Python startup comparison, cold-storage result, or proof that an image is faster than CPython**.

**Proposal — unverified:** produce a reproducible sealed startup image after constructing builtins and importing the safe startup subset. Persist canonical object/type/module/code state plus versioned primitive identities; restore/rebase roots and rebuild ephemeral VM state. Use pre-generated template-tier code or a separately validated cache if it reduces startup cost; optimizing SSA profiles/code are optional and must be invalidated for CPU/VM/ABI/code changes. Persisting Smalltalk's mechanisms does not solve Python C pointers automatically.

The image build/restore contract must specify:

1. **Identity and imports:** preserve singleton/type/module identity, but reconstruct executable paths, `sys.argv`, `sys.path`, environment-sensitive codec/locale/timezone state, std streams, current working directory, and site processing according to each launch's flags. Image restore cannot skip `.pth` or customization semantics. Sources: `Lib/{site,os}.py`, `Python/{initconfig,pylifecycle}.c`.
2. **Hash and entropy state:** do not distribute one production hash secret/PRNG state across starts. Choose fresh secrets and rebuild persisted dict/set tables while invalidating/recomputing dependent cached str/bytes/frozenset hashes, or choose an image representation that reconstructs these objects. Restrict the generic startup image to a known-safe object closure rather than arbitrary user-defined hash functions. Explicit `PYTHONHASHSEED` and random state APIs still apply. Cost is unverified. Sources: `Python/bootstrap_hash.c`, `Objects/{dictobject,setobject,unicodeobject,bytesobject}.c`, `Lib/random.py`.
3. **Threads and resources:** restore no live foreign TLS pointer, fd/socket connection, SQLite statement, libffi closure or OpenSSL session from a generic startup image. Recreate native handles, locks, worker/thread state, signal wakeups, pending exceptions and context state. Initialize library engines after restore unless each resource has a verified reconstruction contract. Sources: §4; `Python/pystate.c`; Smalltalk logical snapshot boundary above.
4. **ABI and integrity:** fingerprint interpreter, Python sources/data, target ABI/architecture/features and primitive manifest; relocate managed references; reject incompatible images before publishing state; retain the source/slow-bootstrap path for development and recovery. Code/native object pointer serialization cannot be inferred safe from Python object serialization. Sources: exposed C layouts in §7; Smalltalk `vm/snap_wire.inc` and `docs/vm/snapshot-code-INTEGRATION.md`.

Future evaluation must compare cold filesystem, warm filesystem, `-S`/normal/isolated starts, source/bootstrap versus image, and first useful import/action; include restore, rehash, stdio/site setup, memory faults and exit cost. No speedup, image size or break-even reuse count is verified by this research. The image is a design candidate, not a reason to omit initialization semantics.

## 9. Recommendation and unresolved decisions

Proceed with a Python kernel and CPython 3.14's existing Python library as the implementation baseline. Keep one explicit semantic primitive manifest, native lifecycle/ABI support, thin platform adapters, and retained external engines. Use the measured fallback cohort as concrete porting input, not as evidence that all C accelerators already have twins. Reference sources and the estimated 50K–95K additional Python work are specified above.

The first implementation study should join **Python dict/list/str + allocation/storage/locking + C layout/slots/buffers + one existing Python fallback + one native library call**, then exercise Python and native observers, including concurrent callbacks and GC. This is a proposed research slice within the day-one full C-API/no-GIL/numpy bar, not a reduced public target. Verification should use unmodified stdlib tests for builtins/modules, their C/Python implementation variants where available, free-threading tests (`Lib/test/test_free_threading/`) and the required unmodified-numpy suite. No such runtime verification was possible or claimed here.

| Open decision | Why it matters / evidence needed |
|---|---|
| Exact conventional/free-threaded CPython ABI and numpy binary target | Full layouts/inline macros/reference semantics must agree before freezing the primitive storage contract; resolve jointly with [01](01-capi-object-model.md). No blanket “C compatibility layer” proves this. |
| Canonical representation and permitted storage strategies | Direct C observers, mutable aliases and buffers may rule out strategies or demand permanent materialization; prototype identity/mutation/retention, not just call/return conversions. `Include/cpython/*object.h`. |
| Container/callback concurrency protocol | Decide lock suspension/reentry, atomic publication, version validation and iterator shared-state rules; test GC/native retention and JIT deopt together. `Objects/{dictobject,listobject}.c`, `Python/critical_section.c`. |
| Numeric kernel boundary | Determine which bignum/float parse/format operations remain Python versus measured native kernels; primitive semantics must preserve rounding and exceptions. `Objects/longobject.c`, `Python/dtoa.c`, `Lib/_pylong.py`. |
| Unicode/codecs/tokenizer/regex implementation route | Versioned data and bytecode/opcode compatibility, locale/surrogate edge cases and matcher resource behavior require complete ports; `Lib/encodings/`, `Lib/re/`, `Parser/lexer/`, `Modules/cjkcodecs/`. PyPy 3.12 is reference material that still needs 3.14 review. |
| Bootstrap and snapshot closure | Measure actual imported modules, initialized objects, serialized bytes, restore/rehash work and first-action cost; choose portable object image versus optional native code cache. §8 supplies source bounds, not timings for Lucifer. |
| Platform/library binding ownership | Choose generated typed adapters versus hand wrappers, resource/thread affinity and callback ABI; preserve module-specific lifecycle semantics. PyPy's 19 source-visible CFFI surfaces demonstrate feasibility, not a 3.14 port. |
| Effort estimate refinement | Replace the 50K–95K envelope with retained-file and work manifests after a vertical slice. Include separate native-runtime/C-API, data, tests and external-library budgets. No staffing/date inference follows from source counts. |

Research verification: script syntax and ShellCheck passed; all input files in its explicit cohorts read successfully; directory/module/source counts regenerated; embedded stdout checked against a fresh run; production Setup/core/Windows module names and all 70 top-level PyPy module definitions checked against inventory coverage; final diff reviewed for scope. No builds, benchmarks, vendor execution, commits or state-changing Git commands were performed. Remaining runtime correctness, performance and effort estimates are explicitly unverified.

## Appendix A. Complete counting-script output

The following is verbatim stdout from `sh docs/research/03-stdlib-count.sh .` for the pinned checkouts above.

```text
CPython physical lines: label	files	lines
Objects/*.c (recursive)	68	123604
Python/*.c (recursive)	100	137638
Modules/*.c (recursive)	251	301011
Parser/*.c (recursive)	16	45816
Lib/*.py excluding Lib/test	718	329050
Lib/test/*.py	1112	630844
Objects headers/includes (supplement)	50	25020
Python headers/includes (supplement)	25	33621
Modules headers/includes (supplement)	233	181956
Parser headers/includes (supplement)	7	619

Modules physical .c accounting: path bucket	files	lines
TOTAL	251	301011
_abc.c	1	997
_asynciomodule.c	1	4421
_bisectmodule.c	1	484
_bz2module.c	1	829
_codecsmodule.c	1	1099
_collectionsmodule.c	1	2876
_csv.c	1	1839
_ctypes/	7	13067
_curses_panel.c	1	788
_cursesmodule.c	1	5353
_datetimemodule.c	1	7731
_dbmmodule.c	1	646
_decimal/	26	21778
_elementtree.c	1	4536
_functoolsmodule.c	1	1935
_gdbmmodule.c	1	877
_hacl/	13	16749
_hashopenssl.c	1	2390
_heapqmodule.c	1	811
_interpchannelsmodule.c	1	3658
_interpqueuesmodule.c	1	1994
_interpretersmodule.c	1	1747
_io/	8	12836
_json.c	1	1949
_localemodule.c	1	1034
_lsprof.c	1	1137
_lzmamodule.c	1	1662
_multiprocessing/	3	1252
_opcode.c	1	452
_operator.c	1	2032
_pickle.c	1	8087
_posixsubprocess.c	1	1351
_queuemodule.c	1	625
_randommodule.c	1	686
_remote_debugging_module.c	1	3105
_scproxy.c	1	258
_sqlite/	9	6380
_sre/	1	3451
_ssl.c	1	6917
_ssl/	3	508
_stat.c	1	713
_statisticsmodule.c	1	159
_struct.c	1	2793
_suggestions.c	1	69
_sysconfig.c	1	109
_testbuffer.c	1	2932
_testcapi/	37	11244
_testcapimodule.c	1	3493
_testclinic.c	1	2450
_testclinic_limited.c	1	153
_testimportmultiple.c	1	68
_testinternalcapi.c	1	2608
_testinternalcapi/	5	1221
_testlimitedcapi.c	1	96
_testlimitedcapi/	21	6291
_testmultiphase.c	1	995
_testsinglephase.c	1	801
_threadmodule.c	1	2824
_tkinter.c	1	3604
_tracemalloc.c	1	228
_typesmodule.c	1	72
_typingmodule.c	1	99
_uuidmodule.c	1	154
_weakref.c	1	187
_winapi.c	1	3240
_xxtestfuzz/	2	765
_zoneinfo.c	1	2787
_zstd/	4	2551
arraymodule.c	1	3283
atexitmodule.c	1	342
binascii.c	1	1332
blake2module.c	1	1074
cjkcodecs/	7	5329
cmathmodule.c	1	1340
errnomodule.c	1	988
expat/	5	14084
faulthandler.c	1	1501
fcntlmodule.c	1	783
gcmodule.c	1	557
getaddrinfo.c	1	642
getbuildinfo.c	1	79
getnameinfo.c	1	207
getpath.c	1	981
getpath_noop.c	1	10
grpmodule.c	1	409
hmacmodule.c	1	1862
itertoolsmodule.c	1	4080
main.c	1	830
mathmodule.c	1	4201
md5module.c	1	410
mmapmodule.c	1	2119
overlapped.c	1	2093
posixmodule.c	1	18249
pwdmodule.c	1	380
pyexpat.c	1	2286
readline.c	1	1660
resource.c	1	552
rotatingtree.c	1	125
selectmodule.c	1	2895
sha1module.c	1	411
sha2module.c	1	954
sha3module.c	1	680
signalmodule.c	1	2071
socketmodule.c	1	9290
symtablemodule.c	1	132
syslogmodule.c	1	469
termios.c	1	1398
timemodule.c	1	2395
tkappinit.c	1	92
unicodedata.c	1	1698
xxlimited.c	1	453
xxlimited_35.c	1	332
xxmodule.c	1	413
xxsubtype.c	1	328
zlibmodule.c	1	2179

Setup module recipes: module	direct .c files	lines
_abc	1	997
_asyncio	1	4421
_bisect	1	484
_blake2	1	1074
_bz2	1	829
_codecs	1	1099
_codecs_cn	1	470
_codecs_hk	1	190
_codecs_iso2022	1	1150
_codecs_jp	1	765
_codecs_kr	1	468
_codecs_tw	1	143
_collections	1	2876
_csv	1	1839
_ctypes	5	11594
_ctypes_test	1	1308
_curses	1	5353
_curses_panel	1	788
_datetime	1	7731
_dbm	1	646
_decimal	1	6335
_elementtree	1	4536
_functools	1	1935
_gdbm	1	877
_hashlib	1	2390
_heapq	1	811
_hmac	1	1862
_interpchannels	1	3658
_interpqueues	1	1994
_interpreters	1	1747
_io	7	11572
_json	1	1949
_locale	1	1034
_lsprof	2	1262
_lzma	1	1662
_md5	1	410
_multibytecodec	1	2143
_multiprocessing	2	1101
_opcode	1	452
_operator	1	2032
_pickle	1	8087
_posixshmem	1	151
_posixsubprocess	1	1351
_queue	1	625
_random	1	686
_remote_debugging	1	3105
_scproxy	1	258
_sha1	1	411
_sha2	1	954
_sha3	1	680
_signal	1	2071
_socket	1	9290
_sqlite3	9	6380
_sre	1	3451
_ssl	1	6917
_stat	1	713
_statistics	1	159
_struct	1	2793
_suggestions	1	69
_symtable	1	132
_sysconfig	1	109
_testbuffer	1	2932
_testcapi	38	14737
_testclinic	1	2450
_testclinic_limited	1	153
_testimportmultiple	1	68
_testinternalcapi	6	3829
_testlimitedcapi	22	6387
_testmultiphase	1	995
_testsinglephase	1	801
_thread	1	2824
_tkinter	2	3696
_tracemalloc	1	228
_types	1	72
_typing	1	99
_uuid	1	154
_weakref	1	187
_xxtestfuzz	2	765
_zoneinfo	1	2787
_zstd	4	2551
array	1	3283
atexit	1	342
binascii	1	1332
cmath	1	1340
errno	1	988
faulthandler	1	1501
fcntl	1	783
grp	1	409
itertools	1	4080
math	1	4201
mmap	1	2119
posix	1	18249
pwd	1	380
pyexpat	1	2286
readline	1	1660
resource	1	552
select	1	2895
syslog	1	469
termios	1	1398
time	1	2395
unicodedata	1	1698
xxlimited	1	453
xxlimited_35	1	332
xxsubtype	1	328
zlib	1	2179

PyPy source partitions: label	files	lines
objspace/std top-level implementation-side Python	39	27640
pypy/module non-test Python (all sides)	501	115654
pypy/module app_*.py subset	34	2553
lib_pypy non-test Python including bundled dependencies	203	58853
lib_pypy/cffi subset	28	15415
lib_pypy *_build.py subset	20	3809
__builtin__
__pypy__
_abc
_ast
_cffi_backend
_codecs
_collections
_continuation
_cppyy
_csv
_demo
_frozen_importlib
_hpy_universal
_io
_jitlog
_locale
_lsprof
_md5
_minimal_curses
_multibytecodec
_multiprocessing
_opcode
_pickle
_pickle_support
_posixsubprocess
_pypyjson
_random
_rawffi/alt
_rawffi
_socket
_sre
_string
_testing
_vmprof
_warnings
_weakref
_winreg
array
atexit
binascii
bz2
cmath
cpyext
crypt
errno
exceptions
faulthandler
fcntl
gc
imp
itertools
marshal
math
micronumpy
mmap
operator
posix
pwd
pyexpat
pypyjit
select
signal
struct
sys
termios
thread
time
token
unicodedata
zipimport
zlib
MODULEDEF_COUNT	71

PyPy per module: module	implementation-side files/lines	app_* files/lines
__builtin__	9/2310	5/388
__init__.py	1/1	0/0
__pypy__	14/1205	2/42
_abc	3/41	1/191
_ast	2/26	0/0
_cffi_backend	35/9150	0/0
_codecs	4/1479	0/0
_collections	4/800	2/247
_continuation	4/474	1/35
_cppyy	12/5927	0/0
_csv	5/798	1/33
_demo	3/105	1/12
_frozen_importlib	3/205	0/0
_hpy_universal	44/7193	0/0
_io	10/5389	1/24
_jitlog	3/41	0/0
_locale	3/398	0/0
_lsprof	3/531	0/0
_md5	3/85	0/0
_minimal_curses	4/247	1/3
_multibytecodec	5/654	1/55
_multiprocessing	5/695	0/0
_opcode	3/51	0/0
_pickle	4/3097	1/148
_pickle_support	3/147	0/0
_posixsubprocess	3/340	0/0
_pypyjson	6/1929	0/0
_random	3/131	0/0
_rawffi	15/2905	1/21
_socket	4/1808	0/0
_sre	4/1393	1/46
_string	3/31	0/0
_testing	2/17	1/6
_vmprof	3/137	1/19
_warnings	3/481	1/15
_weakref	3/451	1/9
_winreg	3/1218	0/0
array	8/1633	0/0
atexit	2/20	1/45
binascii	9/794	0/0
bz2	3/604	0/0
cmath	3/285	0/0
cpyext	58/18003	0/0
crypt	3/38	0/0
errno	3/134	0/0
exceptions	4/1477	1/221
faulthandler	5/423	0/0
fcntl	3/342	1/4
gc	5/882	1/123
imp	5/728	0/0
itertools	3/1976	0/0
marshal	3/553	0/0
math	3/930	1/306
micronumpy	31/16076	1/35
mmap	3/499	0/0
operator	4/320	1/141
posix	6/4955	2/276
pwd	3/159	1/19
pyexpat	3/1151	0/0
pypyjit	6/917	0/0
select	5/956	0/0
signal	3/673	0/0
struct	4/571	0/0
sys	12/2138	0/0
termios	3/168	0/0
thread	8/1082	0/0
time	4/1632	1/89
token	3/44	0/0
unicodedata	3/382	0/0
zipimport	2/32	0/0
zlib	3/634	0/0

CPython candidate reuse cohort (not all standalone fallbacks): file	lines
_pydecimal.py	6389
_pydatetime.py	2734
_pyio.py	2750
pickle.py	1931
bisect.py	118
heapq.py	610
functools.py	1166
collections/__init__.py	1605
json/__init__.py	359
json/decoder.py	364
json/encoder.py	459
json/scanner.py	73
_strptime.py	822
operator.py	475
_py_abc.py	147
statistics.py	1869
stat.py	207
queue.py	384
xml/etree/ElementTree.py	2102
_py_warnings.py	866
tokenize.py	592
_pylong.py	729
zoneinfo/_zoneinfo.py	772

CPython startup source envelope: file	lines
importlib/_bootstrap.py	1562
importlib/_bootstrap_external.py	1559
zipimport.py	822
codecs.py	1125
encodings/__init__.py	177
encodings/aliases.py	560
encodings/utf_8.py	42
encodings/ascii.py	50
io.py	150
abc.py	188
_collections_abc.py	1167
os.py	1190
stat.py	207
genericpath.py	200
posixpath.py	595
ntpath.py	914
site.py	779
_sitebuiltins.py	103
```
