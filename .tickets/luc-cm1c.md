---
id: luc-cm1c
status: open
deps: []
links: []
created: 2026-11-24T14:16:53Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-orn7
---
# boot image: heap snapshot and ahead-of-time kernel units

Every builtin is Python that runs interpreted until hot, every process re-runs the boot in Python and re-compiles everything (luc-1rwj: bare start 154M instructions against CPython's 52M; cold builtin calls 8 to 24 times CPython's; a CLI tool spends 907 ms compiling per run). This design maps a heap written at build time and loads kernel units compiled ahead of time. Stages S0 to S4 below. Not approved to build past S0 (measurements) until the owner decides the items in section 7: boot objects immortal (visible to sys.getrefcount and gc.get_objects), the writer's trigger option, disk size of the units, and that this reopens the image trigger recorded in DESIGN.md. Defaults I will take unless told otherwise: PIE with relocation, the per-process hash secret kept, an error rather than a fallback on a bad image. The design's own estimate for generic ahead-of-time units is 2 to 5 times CPython's C per cold builtin call (from 8 to 24 times), and list.sort stays 15 to 30 times: loops written in Python do not reach C by compiling them generically.


## Design (2026-11-24)


Paths are relative to the repository root at 873459ec (branch luc-cold). I wrote this from reading the code. Nothing was built or run. Anything marked "inferred" is my reasoning, not something read in code or measured.

## 0. Summary

- **What exists.** There is already a build-time code image (`runtime/imagegen.c`, `runtime/interp/image.c`). It holds code objects only, and each `Py_Initialize` copies and relocates it.
- **Proposal: one boot image with three parts.**
  1. The heap, as it stands after the boot phases that read nothing from the process.
  2. Lazy code segments for frozen modules that boot does not run. This is today's code image.
  3. Uninlined, generic compiled units for kernel functions, built ahead of time.
- **Who writes it.** The stage-0 binary writes the image. It is the same build as the final binary, so every binary-relative address is the same in both. The image is attached to the binary afterwards without moving anything.
- **What start does.** With PIE and ASLR kept, a process maps the image, applies one relocation pass (two deltas, driven by a bitmap), rebuilds the dict and set index tables whose hashes changed, and restores a runtime record. It then runs only the per-process phase.
- **What I recommend.** Relocation, not fixed addresses: it handles PIE, ASLR, re-initialization and random hash seeds with one mechanism. I estimate 2–4M instructions at start, against 154M today (inferred). A non-PIE, fixed-address variant would remove that pass. It is a security decision for the owner (§7).

## 1. What exists

**The code image (built by `imagegen`).**
- `build/imagegen` is a separate executable linked without an image (`Makefile:109-116`). It boots by unmarshalling with hash seed 0 (`runtime/imagegen.c:366-381`). It copies every frozen entry's code graph into an area and writes the file.
- Objects in the image are made immortal and untracked: `ob_tid=0`, `ob_ref_local=_Py_IMMORTAL_REFCNT_LOCAL`, `LU_GC_DEFERRED` (`imagegen.c:262-270`).
- Cached hashes are cleared:
  - str hash set to -1 (`:276`);
  - bytes and tuple hashes cleared (`:290-296`);
  - code per-process fields cleared, and `co_version` set to 2 + index (`:297-309`).
- Pointers are recorded in a bitmap. A pointer to a static object outside the image is encoded as "extern index + offset", against a 32-entry table of `_PyRuntime`, singletons and 8 static types (`interp/image.c:40-60`; `imagegen.c:210-218`).
- Frozensets are not stored. They are rebuilt at load from an item tuple, because their tables hold secret-dependent hashes (`image.c:91-108`).
- Interned strs go into an FNV-keyed table with no secret (`image.h:43-45`). Interning falls back to that table on a miss (`object/unicode.c:477`).
- The image is `.incbin`'d into `.rodata` (`runtime/image.S`). Each `Py_Initialize` maps fresh anonymous memory, copies segment 0 into it and relocates it. A module's segment is copied when its code is first asked for (`image.c:77-153`).
- The collector reaches image code through `lu_image_each_code` (`collector.c:537`).
- Size today: 7.7 MB (`build/image.bin`).

**Static and immortal objects.**
- The 215 exported data objects are static and immortal (`DESIGN.md:205-209`).
- Static types are readied process-wide: index, flags, an immortal MRO and bases tuple, and a version tag from a process counter (`prim/type.c:1014-1051`).
- Their dicts are per interpreter: `interp->static_types[i].tp_dict` (`type.c:1080-1095`; `lucifer.h:188-191`).
- The kernel fills in their slots from Python while it boots.
- Every writable global is inventoried, with a reason, in `tools/abi/globals.tsv`, which `check-globals` enforces (`Makefile:237`).

**Boot order** (`pylifecycle.c:555-621`):
1. Config is read; SIGPIPE and SIGXFSZ are set to ignore (`:570-575`).
2. The hash secret is set (`:577`; `object/hash.c:36-61`: `getrandom` unless `PYTHONHASHSEED`).
3. The main task is created (`:580-585`).
4. `lu_layouts_init` runs (`:587`).
5. `boot_native` (`:243-311`): the interned dict, `lu_image_map`, the `LU_ID` strs, `sys.modules`, the builtins and sys modules, `lu_types_init`, `add_builtins`, `lu_cc_init`, the last-resort `MemoryError`, sys functions, `sys.monitoring`, `_lucifer`.
6. `lu_kernel_boot` (`interp/frozen.c:92-134`): each kernel module body runs, exports go into builtins, `lu_static_types_inherit` runs, and `specializing` and `kernel_booted` are set.
7. `lu_sys_configure` (`sysmodule.c:171-179` → `lib/kernel/sys.py:441-486`).
8. `lu_import_init` (`interp/import.c:489-521`).
9. `lu_sys_init_streams` (`sys.py:539-549`, which imports `_io`).
10. `__main__`, faulthandler, perf, sched.
11. site (`pylifecycle.c:612-619`).

**State that boot creates.**
- **Interpreter fields** (`lucifer.h:505-575`): `modules`, `builtins`, `sysdict`, `interned`, `image`, `static_types[]`, `kernel[]`, `kernel_rebound`, `kernel_twins`, `kernel_nexts`, `next_version_tag`, `version_types` (a borrowed tag→type table in raw memory), `next_keys_version`, `next_func_version`, watchers, `contextvar_type`, `specializing`, `mcache`, `kernel_booted`, `max_str_digits`, `importlib`, `import_func`, `config`, gc counters.
- **`_PyRuntime` fields** (`lucifer.h:1130-1213`): `ids`, `next_static_version_tag`, `next_static_type`, `next_module_index`, the identifiers array, the signal handler table.
- **Static data from `globals.tsv`**: `lu_exc_*` and the private static types, `lu_inittab`, `lu_cc_strings`, `lu_cc_dummy_name`, `next_serial_counter` (dict keys serials).
- **Heap objects**: functions, code, module dicts, heap types with their dicts, MROs and shared keys, `PyCFunction` objects over raw `lu_kernel_def` blocks, descriptors, instances with inline values and preheaders, weakrefs.
  - Non-object blocks are reached from objects. Examples: dict keys tables, list arrays, and `lu_kernel_def`, whose `ml_name` and `ml_doc` are interior pointers into strs (`object/descr.c:74-90`).
- **Specialized inline caches and JIT feedback** are written into code objects. The branch counters are written even with the JIT off (`jit/jit.h:182-191`).

**What depends on the process.**

| Item | Where |
|---|---|
| Addresses under ASLR | the binary is PIE (`readelf`: DYN); heap mmaps are randomized |
| Hash secret | `hash.c:36-61` |
| Identity hashes of types and functions used as keys | they are addresses |
| argv, flags, path, prefixes, `_xoptions`, `warnoptions` | `sys.py:441-486` |
| `os.environ` / `posix.environ` | `lib/modules/posix.py:224-233`; `Lib/os.py:799` |
| Time zone and current time | `lib/modules/time.py:257-282`, run at import |
| SIGINT handler installed at import | `lib/modules/_signal.py:135-138` |
| Locale and stdio encoding | `sys.py:497-518` |
| Standard streams and fds | `sys.py:521-549` |
| Frozen modules' `__file__` (from `sys._stdlib_dir`) | `Lib/importlib/_bootstrap.py:1108-1128` |
| fs encoding captured at `os` import | `os._fscodec`, inferred from `Lib/os.py` |
| Thread ids, pid | |
| mimalloc heaps, `ob_tid`, biased-refcount queues, QSBR queues | |
| JIT code memory and dependency buckets | |
| `int_max_str_digits` | `pylifecycle.c:245` |

## 2. The heap snapshot

### 2.1 Where the writer runs

Pointers into the binary are everywhere: `ob_type` of almost every object, type slots, `ml_meth`, getset and member tables. They can be relocated only if the writer has the reader's layout. `imagegen` today is a different link (`Makefile:109`), which is why it can encode only a hand-written extern table (`image.c:40-60`).

Design:
- Link `build/lucifer0` from exactly the objects of `bin/lucifer`, minus the image.
- `build/lucifer0` writes the image. Every binary pointer is encoded as an offset from the load base.
- `bin/lucifer` is produced by attaching the image to `build/lucifer0` without moving any loadable section. Use `objcopy --add-section .lu_image=…` as a non-allocated, page-aligned section.
- The build then checks that program headers and the `nm` addresses are identical in `build/lucifer0` and `bin/lucifer`. That check is part of the write.
- At start, the runtime opens the executable (the path `config.c` already resolves, or `/proc/self/exe`), finds the section through the section headers and maps it privately.
- `imagegen.c` and `image.S` are then deleted. The writer takes over their lazy code segments.
- Inferred and unverified: that objcopy keeps every PT_LOAD unchanged, and that `strip` keeps the section. The layout check guards the first.
- Alternative: keep `.incbin`, with a linker-script line that places `.lu_image` after `.bss`.

The writer needs a trigger. I propose `-X lucifer_image_write=PATH`, accepted only by a binary that has no image. It is a build step, like CPython's `_freeze_module`. It is the owner's call (§7).

### 2.2 Fixed address or relocation

**Fixed address with zero relocation** needs all of these:
- a non-PIE binary, because `ob_type` points into it;
- the image mapped with `MAP_FIXED_NOREPLACE` at an address chosen at build time;
- a fallback relocation path anyway, for `Py_Finalize` followed by `Py_Initialize`. Earlier copies stay mapped (`image.c:6-8`; `pylifecycle.c:667-700`), so a second interpreter cannot take the same address.

It also loses ASLR for the executable, and puts writable function pointers (type slots) at known addresses.

**Relocation** with two deltas (image base and binary base) extends today's bitmap pass (`image.c:82-90`).
- Estimate (inferred): a boot heap of 6–12 MB holds about 150–300k pointer words, about 1–1.5M instructions to relocate.
- It touches every page that holds a pointer, so the copy is private, about the image's size per process.
- `MADV_HUGEPAGE` on the anonymous copy keeps page faults to a handful.

**Recommendation:** relocation. It is one mechanism for PIE, ASLR and re-initialization, and it is the existing scheme.

### 2.3 Immortal mapping, outside mimalloc

Image objects stay immortal, as today:
- `ob_tid=0`, immortal local count, `LU_GC_DEFERRED` set, GC-tracked bit clear;
- they are never in a mimalloc heap and never freed;
- `ob_mutex` and container storage stay writable, because the copy is private.

What follows from that:
- **Reference counts.** `_Py_IsImmortal` short-circuits every increment and decrement on every thread. No thread owns an image object, so critical sections take the lock, or skip it while serial. Uniqueness is always false.
- **Collector.** It never visits image objects (`collector.c` invariant 5, `:23-24`). Heap objects stored into image containers hold a counted reference, so trial deletion keeps them alive.
- Two collector changes are needed:
  - (a) `mark_alive_from_roots` skips immortal objects (`collector.c:493`). It must traverse through image containers without marking them, or the "most of a program's heap" fast path (`:485-523`) is lost.
  - (b) The caches pass (invariant 7, `:38-43`) must visit every image code object. The writer stores the list, as `image.h` does now.
- **Storage frees.** A container that grows after start retires its old storage, which may be image memory:
  - `dict.c:310`, `:790`, `:951`;
  - list arrays in `prim/storage.c:70`, `:114`, `:345`;
  - shared keys through `dk_refcnt`.

  `lu_qsbr_retire` → `free_item` → `PyMem_Free` (`thread/qsbr.c:147-155`) would then call `mi_free` on a foreign pointer. Add one range check in `lu_mem_free` (`object/alloc.c:208-211`): image storage is never freed. The alternative, adopting objects into mimalloc one by one, costs an allocation per object (about 6–10M instructions, inferred) but is fully CPython-observable. I keep it as the fallback if the floor loses tests to immortality (§7).

### 2.4 Types, versions and caches

- Version tags are counters. The image keeps them, and the record restores `next_version_tag`, `next_keys_version`, `next_func_version`, `next_static_version_tag` and `next_serial_counter`.
- `version_types` is rebuilt at map time from a list of (tag, type) pairs in the image.
- `mcache` starts zeroed (the interpreter lives in `.bss`).
- Before writing, the writer resets the inline caches of image code in place. It uses the logic of `lu_code_bytes` (`interp/marshal.c:1043-1062`), and also clears `co_executors` and the feedback words. Image code is then pristine, as today.
- JIT dependencies are keyed by object address (`jit/deps.c:68-73`). An image object's address is fixed for the life of the process, so nothing changes there.

### 2.5 Hash randomization

Lucifer randomizes str hashes per process (`hash.c:36-61`). The harnesses set seed 0 (`DESIGN.md:209-213`). The design:

1. The writer uses seed 0, as `imagegen` does (`imagegen.c:377`). All str, bytes, tuple and frozenset hash caches are stored as -1.
2. The interned dict is stored empty. Every interned image str goes into the FNV table, which already exists.
3. The image lists every dict, set and frozenset table whose layout depends on hashes. Each table is tagged with whether it holds secret-hashed keys (str, bytes, tuples of them) or identity-hashed keys (types, functions, weakrefs).
4. At map time:
   - if the secret is not all zero, rebuild the index arrays of the secret-hashed tables in place (same size, same entries order, so insertion order and `dk_version` are kept);
   - if the base moved (always, under ASLR), rebuild the identity-hashed tables.

   Estimate (inferred): 10–15k entries, about 1M instructions. Under `PYTHONHASHSEED=0` the str pass is skipped.
5. Sets and frozensets are rebuilt from their item order under seed 0, as frozensets are today. Order under a fixed non-zero seed may differ from CPython's insertion history (open question 4).
6. The writer refuses a key in an image table whose hash would run Python (a user-defined `__hash__`).

The per-process secret, and so its security property, is unchanged.

### 2.6 Snapshot phase and per-process phase

**Snapshot phase.** It must read no process input and no config. In order:
- `boot_native`, minus `max_str_digits`;
- `lu_kernel_boot`;
- the constant part of configure: `builtin_module_names`, which importlib's `_setup` reads (`_bootstrap.py:1517-1550`);
- `lu_import_init`;
- importing `_io`, `codecs`, `encodings`, `encodings.utf_8`, `time`, `posix`, `_signal`.

**Per-process phase:**
- config, secret, task;
- map and relocate the image, restore the record;
- registered per-process hooks:
  - `posix.environ`;
  - `time`'s `tzset` and `_init_timezone`;
  - `_signal`'s SIGINT install;
  - later, `os.environ` and `os.fsencode`;
- configure (argv, flags, path, prefixes, `_xoptions`, `warnoptions`, executable, `pycache_prefix`), and `max_str_digits`;
- dev-mode and `-W` filters;
- streams and `builtins.open`;
- `__main__`, faulthandler, perf, sched;
- site, both its body and `main()`. That covers user site, `.pth` files, `sitecustomize`, `usercustomize` and `pyvenv.cfg`;
- `prepend_main_path`;
- a `__file__` fix-up for preloaded frozen modules, only when `_stdlib_dir` differs from the writer's.

A hook is registered by the module, through a `_lucifer` primitive, during the snapshot phase.

**Site's imports.** `os`, `stat`, `posixpath`, `genericpath`, `_collections_abc`, `_sitebuiltins` and `abc` cannot simply sit in `sys.modules`: `-S` (every kernelcheck program runs `-I -S`, `DESIGN.md` §7) must not show them. So the image keeps them as a preloaded set:
- each with the exact sequence of `sys.modules` insertions its import made;
- an import of one installs the set by replaying that sequence, only if none of its members is already present;
- otherwise the import runs normally.

### 2.7 Write-time validation (the writer validates, boot only maps)

**V1, in the writer.** It fails loudly on any of these:
- an object type or raw block it has no precise layout for;
- a pointer outside the image or the binary;
- a held lock, a live frame, a pending call, or a weakref callback still queued;
- any call of a process-input or process-effect primitive outside a hook. The writer swaps the `ml_meth` of the `lu_prim_os`, `signal`, `time`, `passwd` and `locale` tables for traps in its own process only, so there is no runtime cost;
- an image int equal to the writer's pid, a thread id, or "now";
- a `build/lucifer0`/`bin/lucifer` layout difference;
- two writes that are not byte-identical (reproducibility).

**V2, `check-image`.** This is a test and needs no new switch. Run a canonical state dump on `build/lucifer0` (boots from Python) and on `bin/lucifer` (maps), and require equality.
- The dump is ordinary Python:
  - `sys.modules` in order; for each module, the key order and a value summary (type, qualname, repr of simple values);
  - every reachable class's MRO and dict keys;
  - `sys.*`, `os.environ`, `time.tzname`, `signal.getsignal` for each signal;
  - the codec search cache, warnings filters, `meta_path`, `path_hooks`, `__file__`s;
  - a `_lucifer` digest of the C record: counters, and static types' flags, tags and slots as offsets.
- The config matrix: default, `-S`, `-I`, `-E`, `-X dev`, `-W error`, `-b`, `-O`, `-X utf8`, `PYTHONHASHSEED` 0, 1 and random, `LC_ALL` C, C.UTF-8 and ISO-8859-1, two `TZ` values, `PYTHONPATH`, another working directory, other argv.

**V3.** Every existing lane runs on the imaged binary.

## 3. Precompiled kernel units

### 3.1 What a unit embeds today

- **Absolute 64-bit immediates for every address** (`x86_64/emit.c`, 53 `mov_ri` address sites). These include:
  - helpers through `call_abs` (`:733-738`);
  - `_PyRuntime` fields (`:2282`, `:2687`);
  - static types;
  - the unit's own data: `dead` (`:2109`), `counts` (`:1788`), `outs[].left` (`:5208`), `stress_counter` (`:1568`);
  - allocation-site records (`:2959`);
  - object constants.
- **Exit-table and descriptor addresses**, patched into the buffer before publication (`:1517-1522`, `:1806-1810`).
- **Code is written once and never patched after it is published** (`:5737-5745`; `codemem.c` dual mapping).
  - Death is a data flag that the code reads.
  - Counters live in unit data, not in code.
  - So code pages could be shared, but only if the emitter became position-independent.
- **Keying.** A unit is keyed by code, globals and builtins (`target.h:168-176`), checked by `GUARD_ENTRY` (`eval_flow.c:2186-2192`).
- **Dependencies.** Each holds a type version or an object address (`deps.c`), plus `LU_DEP_SERIAL` for lock-free stores while serial (`facts.c:200-245`; killed by `task.c:125`).
- **Raw pointers in exit descriptions:** code, function, type, `lu_prof` (`target.h:113-160`).
- **CPU dependence.** The only CPU-dependent codegen is the JCC-erratum NOP padding, keyed on CPUID (`emit.c:349-400`). No other ISA extensions are used. A unit is valid on any x86-64; padding changes only speed.

### 3.2 Ahead-of-time units

After S1, kernel globals and builtins are image objects, so a unit for (kernel code, kernel globals, builtins) can be relocated against the image.

**Building them.** The writer compiles each kernel function's call entry:
- from a synthetic frame: the function, its globals and builtins, no live values. The unboxing speculation at `eval_flow.c:2201-2218` is then skipped;
- with the "most sites never ran" deferral bypassed (`jit.c:902-925`);
- with no class tables asked for;
- without the serial dependency, so a unit never dies when a second thread appears. Locked stores cost something, but hot sites get customized units anyway;
- without padding, so the image does not depend on the build host.

**What the emitter must record.** `mov_ri` of an address becomes `mov_addr(kind)`, which records a relocation (binary, image or unit data). The writer also scans each unit's immediates: any value that falls inside the image, binary or unit ranges without a relocation record fails the write.

**What the image holds** for each unit: code bytes, relocations, descriptor blocks (pointers encoded), constants and dependency records with their expected values.

**Loading and entry.** Laziness is the key:
- `co_executors` gets a third tagged state, "image unit k" (bit 1; `jit.h:91-131`).
- On the first entry: create the JIT state, copy the code into the code pool, apply relocations, and verify each dependency against the current state under the compile ticket protocol (`deps.c:76-139`; the dict is watched first).
- Then publish, or mark the unit dead if a fact changed. That covers a user who rebound `builtins.len` before the first call.
- Later kills are ordinary. A dead image unit is simply not loaded again.

**Coexistence with customization.**
- The image unit is the callee of last resort, reached from the interpreter and through direct entries. lt-98e0's call cells fit this.
- Hot sites still inline and customize (`kernel-customization.md` §1–3).

**JIT off.** `-X lucifer_jit=off` already runs no compiled code (`jit.h:62-66`), so image units are never entered and the kernel runs interpreted. Nothing new is needed.

**Deopt stress.** The deopt stress lane would decline image units. They come from the same builder and emitter that the lane already covers; owner's call (§7).

**Code-page sharing.** Copy-on-first-entry costs a few thousand instructions per unit, against about 1.7 ms per compile today (907 ms / 523 units). It gives no sharing between processes. Sharing would need a position-independent emitter: RIP-relative loads from a per-unit relocated pool, mapped read-only and executable straight from the image. That is a later stage.

### 3.3 How good generic units are (inferred)

Expected IR for an uninlined kernel root with no feedback:
- `GUARD_DEPTH` and `GUARD_ENTRY`;
- globals and builtins folded to constants with value dependencies;
- attribute loads through `h_load_attr_pic` with no PIC entries;
- `BINARY_OP` and `COMPARE_OP` through `h_binary_op` and `h_compare_op`, which have C fast paths for exact int, str, bytes, list and tuple (`helpers.c:717-746`);
- known kernel callees inlined within `INLINE_BUDGET` (1000), otherwise called directly;
- `LU_METH_NOPY` primitives called directly;
- generic iteration;
- boxed locals.

That is baseline-JIT quality. It removes dispatch (about 12 instructions per bytecode) and the about 650 instructions per kernel frame that is inlined away. It keeps the helper costs.

**Training (S4).** The corpus would fill the kernel's merged cache words. That gives better guards, at the risk of corpus-specific exits, which cost about 4,700 instructions each (`jit.c:69-75`). Add it only where a measurement shows a gain.

## 4. What it removes (from the luc-1rwj numbers)

**Startup, bare start (154M).**

| Stage | Removes | Bare start after (inferred) |
|---|---|---|
| S1 | kernel boot 24M and most of "image map, native init" 5M; map, relocation and rehash add back 2–4M | about 130M |
| S2 | importlib 33M; `_io`'s import (13M less stream construction, split unknown); site's imports (unknown share of 67M; `site.main()` remains) | 35–60M |
| S3 | the per-process Python now runs over compiled kernel; the JIT's 19M of boot compiles and hooks largely go | 25–45M (CPython: 52M) |

All ranges are inferred.

**Cold builtins.** The ticket has no data for compiled-but-uninlined kernel calls. These ratios are composed from its tables:

| Call | CPython | Interpreted today | AOT estimate | AOT / CPython |
|---|---|---|---|---|
| `str(i)` | 905 | 21,921 | 3–5k | 3.5–5.5x |
| `s.upper()` | 416 | 3,487 | 1.4–1.7k | 3.5–4x |
| `sum(l100)`, per item | 83 | 2,194 | 150–250 | 2–3x |
| `list.sort`, per item | 60 | 6,794 | 1.0–2.0k (hot-compiled is 908) | 15–30x |
| `d.get(k, 0)` | 699 | 1,225 | 700–900 | 1.0–1.3x |
| `l.append(i)` | 219 | 715 | 400–500 | about 2x |

How to measure it today:
- `LUCIFER_JIT_ONLY` (`config.c:1163-1165`) set to the kernel qualnames leaves user loops interpreted and kernel functions compiled. That is the "with feedback" variant.
- The generic variant needs a measurement-only patch that ignores feedback.

**Per-process compile time.**
- How to find the kernel share of pyflakes' 523 units: run pyflakes once with `LUCIFER_JIT_DEBUG=1`. Each "jit: compiled" line prints the filename (`jit.c:988-992`). Kernel and port code is `<frozen NAME>` (`tools/freeze:77`), so count lines and bytes by filename. Add the unit's milliseconds to that line to get the time share.
- AOT removes only kernel-root compiles. Kernel code inlined into user units is still compiled. Spending less of the inline budget at lukewarm sites, by calling the image unit instead, is luc-1rwj cause 2's policy question.

## 5. Build and determinism

**Makefile flow:**
1. Link `build/lucifer0`, the same objects with no image.
2. `build/lucifer0 -X lucifer_image_write=build/boot.image`. The writer forces seed 0 and the JIT off for the heap, then compiles units.
3. `objcopy` attaches the image to make `bin/lucifer`.
4. The layout check.
5. `check-image` runs V2, plus a second write compared with `cmp`.

So yes, it is a two-stage build. The build pays about 5 s for roughly 3k unit compiles (inferred).

**Reproducibility.**
- Traversal order is canonical.
- Seed is 0.
- Identity-hashed tables are canonicalized against the base used at link time.
- No timestamps.
- No padding in units.

**Size (inferred):**
- heap part, 6–12 MB;
- lazy code, the rest of today's 7.7 MB;
- units, 25–45 MB if limited to the kernel and port functions the corpus reaches; all 3,253 functions would be 50–100 MB.

Only the heap part is touched at start. Units are copied only when entered.

**Staleness.** The image lives inside the binary that wrote it.
- The header carries magic, format version, the writer's GNU build-id and struct sizes.
- On a mismatch or damage, start fails with a message, exit 1. There is no silent fallback.
- A binary built without an image (stage 0, the embedder in `check-embed`) boots from Python. That is the documented behaviour of an imageless build, not a fallback.

**Test lanes** run on `bin/lucifer`:
- JIT off: image heap, interpreted kernel.
- JIT on and always: image units.
- Deopt stress: see §3.2.

`check-image` and a few kernelcheck programs on `build/lucifer0` keep the path that boots from Python alive.

## 6. Stages

Each stage is test first and passes the quick tier.

| Stage | Content | Files | Lines (inferred) | Removes |
|---|---|---|---|---|
| S0 | Measurements, no runtime change: site import vs `site.main()` vs streams (`-X importtime`, perf); kernel-unit share in pyflakes; `LUCIFER_JIT_ONLY` cold-call numbers; Lucifer's `sys.modules` at start vs CPython's with and without `-S` | ticket notes | about 30 | answers open questions 1–3 |
| S1 | Snapshot after `lu_kernel_boot`. The writer moves into the runtime with per-layout pointer maps (str, bytes, int, float, complex, tuple, list and arrays, dict and keys, set, code, function, cell, module, heap type and tail, `PyCFunction` and `lu_kernel_def`, the four descriptor kinds, class/staticmethod, property, weakref, instance with preheader and inline values, slice). Binary-base encoding; the runtime and static-type record; map, relocation and rehash; per-process boot split; free guard; collector changes; `globals.tsv` gains a class column (boot, process or constant) that check-globals enforces; build stages; V1 and V2. Deletes `imagegen.c` and `image.S`. | `runtime/interp/image.c` and `.h`, new `runtime/interp/imagewrite.c`, `pylifecycle.c`, `prim/type.c`, `object/alloc.c`, `thread/qsbr.c`, `object/dict.c`, `interp/collector.c`, `interp/config.c`, `Makefile`, `tools/abi/globals.{py,tsv}`, `tests/image/` | +2,300–2,700, −470 | about 22M of 154M |
| S2 | importlib, `_io`, codecs and encodings, `time`, `posix`, `_signal` in the snapshot. Per-process hook registry; trap primitives in the writer; constant configure split; preloaded set with insertion replay for site's imports; `_stdlib_dir` fix-up; more layouts (structseq, lock objects, WeakSet). The config matrix in V2. | `lib/kernel/sys.py`, `lib/modules/{posix,time,_signal}.py`, `interp/import.c`, `sysmodule.c`, `image.c`, `imagewrite.c` | +900–1,200 | 70–100M (inferred) |
| S3 | AOT units for the kernel, then the ports (S3b). Address relocations in the emitter; generic build mode; serializer; tagged `co_executors`; loader with dependency verification. Tests: kernelcheck in the jit and always lanes; a write-time immediates scan; a test that rebinds a builtin before the first call and checks the unit dies. | `x86_64/emit.c`, `jit/jit.c`, `jit/deps.c`, `jit/eval_flow.c`, `jit/codemem.c`, `imagewrite.c`, `image.c` | +1,500–2,500 | cold kernel interpretation; kernel-root compiles |
| S4 | Training profiles from the corpus. | `jit/jit.c`, `Makefile`, `tools/` | +400–700 | better unit quality, measured |
| S5 (optional) | Position-independent emitter: shared read-only executable image code. Possibly non-PIE fixed address (owner). | `x86_64/emit.c` | about 400 | relocation and copy |

**Hazards and their write-time checks:**
- **S1.** A missed pointer field: caught by the per-layout maps, the out-of-range pointer check, and the V2 digest. A missed boot-written global: caught by the `globals.tsv` class column. A storage free reaching image memory: a stress test that grows `sys.path`, `builtins` and the type dicts.
- **S2.** Hidden config or environment capture: caught by the trap primitives and the V2 matrix. Parity of `sys.modules` order: V2.
- **S3.** An address immediate without a relocation: caught by the scan. A stale fact at load: caught by verification.
- **S4.** Exit storms: measured with `-X lucifer_jit_stats`.

## 7. Open questions and owner decisions

**Open questions** (not resolvable by reading):
1. How the 67M of site splits between its imports and `site.main()`; and how the 13M of `_io` splits between the import and stream construction.
2. Whether Lucifer's start-up `sys.modules` already matches CPython's, with and without `-S`.
3. Whether `objcopy` keeps every loadable segment unchanged, and whether `strip` keeps the section.
4. Set order under a fixed non-zero `PYTHONHASHSEED` after rebuilding from seed-0 order.
5. Real heap-image size, pointer count and dict entry count. These are the first output of S1's writer report.
6. The kernel share of pyflakes' units and compile time.
7. How good generic units really are (needs the measurement patch).
8. Whether any boot-phase Python stores hashes or addresses. V2 under several seeds will show it.

**Decisions for the owner:**
- **The writer trigger.** I propose `-X lucifer_image_write`, honoured only by a binary that has no image.
- **Security.** PIE with relocation, which I recommend, against non-PIE with a fixed address. Keep the per-process hash secret (about 1M instructions to rehash); I recommend against a fixed build-time seed.
- **Immortality of boot objects, which changes what programs can observe:**
  - `sys.getrefcount` on them;
  - `gc.get_objects`, `get_referrers` and `is_tracked` exclude them (could be mitigated with an image object index);
  - `__del__` and weakref callbacks never fire for them.

  The alternative is adopting them into the heap, at about 6–10M instructions.
- **Startup output.** `-X importtime` and `-v` would no longer print the startup imports.
- **`sys.modules` contents and order at start.** Parity is promised through the preloaded set.
- **Disk size of the units** (25–100 MB).
- **Error on a bad image, not a fallback.**
- **Deopt stress with image units.** Decline them, or compile in a stress check that reads a global word.
- **AOT units without the serial dependency** (locked stores) against serial units that die when the first thread is created.
- **Note:** `DESIGN.md:1022` set the image trigger at "kernel init over 50% of a `-c pass` over 4x the oracle". Today that is 89% but only 3.0x, so building this reopens that decision.

### Files
runtime/interp/image.c, runtime/imagegen.c, runtime/pylifecycle.c, runtime/jit/x86_64/emit.c, runtime/jit/jit.c.
