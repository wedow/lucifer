---
id: luc-uqiy
status: open
deps: []
links: []
created: 2026-11-21T11:05:56Z
type: feature
priority: 1
tags: [startup, boot]
---
# startup: build-time snapshot of the booted kernel heap

-S -c pass runs 147M instructions vs CPython 3.14t's 36M; per luc-w2ra about 64M is running the kernel's module and class bodies (state CPython has as static C types) and about 31M is unmarshalling the kernel's 784 KB of code (40 instructions a byte). Map the booted kernel heap from an image made at build time instead of rebuilding it per process.


## Notes

**2026-11-21T11:06:21Z**

Design (11-21).

Census of the booted heap (lucifer -S, walk from builtins, _lucifer, sys and type subclasses through gc.get_referents, master 1ddf3ecf): 29.6k objects, 4.7 MB by getsizeof, 250k pointer fields counting ob_type, 38k dict entries in 484 dicts, 7.5k str (224k chars), 2.0k code, 2.2k functions, 274 types, 12k ints. Estimated per-process load cost: relocation of the pointers into the binary (about one ob_type per object plus PyMethodDef and slot pointers) under 0.2M instructions with the image at its preferred address, 0.8M relocating every pointer; lazy str hashing at most about 0.8M; lazy dict index rebuild at most about 0.8M for the dicts actually touched. Ceiling: -S -c pass from 147M to roughly 55M (95M removed, under 3M added). Not prototyped end to end.

Boundary. The image is the heap after boot_native and lu_kernel_boot, before lu_sys_configure: neither reads the environment (only the hash secret, set before boot, and int_max_str_digits leak in, both handled below). lu_sys_configure, lu_import_init, the std streams, site and everything env-dependent (sys.flags, argv, paths, os.environ) stay per process, run after the map as today.

In the image (deterministic): the interned-string table, the LU_ID strings, builtins, sys and _lucifer module dicts as boot leaves them, the kernel module namespaces, kernel functions, code, cells, kernel heap classes, the static types' tp_dict contents and slot state (update_slots results), descriptors and wrappers, exception types' dicts, interp->kernel_rebound; and the code objects (unexecuted) of the builtin ports and frozen modules, so their imports skip unmarshal too.

Per process: addresses (relocation, below); str hashes stored as -1 and computed on first use with the process secret, so PYTHONHASHSEED randomization behaves exactly as CPython's for every str, including kernel ones a user dict holds; dict and set index tables stored marked stale and rebuilt on first lookup (address-hashed keys change with relocation too; insertion order is the entries array, so iteration is unaffected); when the process secret and placement equal the build's (PYTHONHASHSEED=0 at the preferred address) the stored hashes and indexes are valid and nothing is rebuilt; type version tags reassigned at map; int_max_str_digits stays an interp field read at use. Never in the image: tasks, carriers, thread state, locks with owners, JIT code and feedback (the dumper refuses an object with thread affinity or a JIT pointer; code counters are zero), file objects.

Mapping. A stage-0 binary (bin/lucifer built with the marshal blobs) is run once by make through a build-only entry point (its own main, not a runtime option) to write build/image.o: the objects laid out contiguously with immortal headers (ob_ref_local UINT32_MAX, ob_tid 0), pointers stored image-relative, plus a compact relocation list of the pointers into the binary (static types, Py_None/True/False, PyMethodDef, C slot functions). The final link embeds it as a section. At start it is mmap'd MAP_PRIVATE from /proc/self/exe at a preferred address (MAP_FIXED_NOREPLACE): image-internal pointers need no fixup there, binary pointers take the ASLR slide; elsewhere a full relocation pass runs; where the file cannot be mapped (unusual embedders) the section is copied. Untouched pages stay shared page cache across processes; writes (type dict updates, in-place specialization of kernel bytecode) dirty only their pages.

Memory. Image objects are immortal and never deallocated. Storage that can be resized away from the image (dict keys, list items, set tables, bytearray buffers) goes through one range check, lu_image_contains(p), before mi_free. Storing a heap object into an image container increfs it as usual.

GC. Image objects are outside the mimalloc heaps, so the collector never visits them; their references to heap objects count as external references, so the referents stay reachable, exactly as CPython treats static types. gc.get_objects does not list image objects (CPython does not list static types either; deepfrozen code was the same).

JIT. Kernel code in the image starts cold; feedback, compiled code and the kernel global folding tables live outside it and start empty, except kernel_rebound which is boot state and comes from the image.

Embedding. Py_FinalizeEx unmaps the image (nothing is freed, all immortal) and the next Py_Initialize maps a fresh private copy: every cycle starts pristine with no reset code; pointers from C statics into the image (static types' tp_dict, LU_IDs) are reassigned at each map. Subinterpreters are unimplemented; when they come, the image's types and kernel functions are runtime-global like CPython's static types and each interpreter copies the module dicts it owns.

Fork. The private mapping is inherited copy-on-write; no fd is kept; nothing to do.

Lazy unmarshal (lever 2): subsumed. Image code objects need no unmarshal at all, and stage 1 already covers the builtin-port and frozen code; lazy unmarshal could save at most the share of the 31M belonging to functions never called, and nothing of the 64M.

Stages (LOC, verification cycles: a cycle is the targeted lanes plain, jit, jit-deopt plus check-embed and the fork, hash-seed and gc:N neighbours):
1. Code image: writer, section, mapper, relocation, immortal headers, lazy str hash, stale frozenset/dict index, image-range free check, interning of image strings; lu_kernel_boot and frozen imports take code from the image instead of PyMarshal_ReadObjectFromString. Wins up to 31M. About 900 LOC (writer 350, mapper and relocation 250, interning 100, build step 50, free checks 50, tests 100), 3-4 cycles.
2. Heap image: functions, cells, dicts, modules, heap classes, static types' dict and slot state, descriptors and wrappers, LU_IDs and kernel_rebound as roots, type version reset; lu_kernel_boot becomes the map. Wins about 64M more. About 1500-2000 LOC, 6-8 cycles (add gc:1 and handoff lanes).
3. Finalize/Initialize remap and moving the boundary past the env-independent half of lu_import_init (the importlib bootstrap modules). About 300 LOC, 2 cycles.

Not attempted in the 11-21 lane: stage 1 alone is about 900 LOC with several cycles on a shared host, so nothing half-done was landed. The census walk also found luc-pplm (an internal sentinel leaks through gc.get_referents and corrupts lists and dict iteration); the dumper walks the heap in C and does not depend on it.

**2026-11-21T13:48:46Z**

Stage 1 landed in 526804bc: build/imagegen (linked without an image, boots by unmarshal) lays out the kernel, builtin-port and frozen code (5930 code objects, 47.6k objects, 7.2 MB, 179k pointers) as immortal objects with unset str/bytes/tuple hashes; the executable carries it read-only (runtime/image.S). Each Py_Initialize mmaps a private area and copies+relocates segment 0 (17k interned strs and multiply-reached objects, 1.7 MB) at boot, and each frozen entry's own segment on first lu_frozen_code; source pages are dropped after the copy. Pointers into the binary go through a fixed table of static types/singletons (lu_image_externs). lu_intern consults the image's FNV intern table on a miss, so LU_IDs and runtime interning return image strs. Frozensets are rebuilt at segment load (hashes take the secret). The collector's cache-forget pass walks loaded image code. Finalize/Initialize maps a fresh copy (old copies stay mapped, as the old cycle's objects are never freed); fork needs nothing. Deviation from the design: no preferred address/MAP_FIXED_NOREPLACE or /proc/self/exe mapping: every object's ob_type points into the PIE binary, so relocation dirties every page anyway; a copy plus per-module laziness gave the same pages for far less code. Numbers (instructions:u, PYTHONHASHSEED=0, min of 5, base = master a1471210 at the same path): -S -c pass 124.42M -> 93.02M (-25%); -c pass 216.08M -> 179.54M; -S -c 'import os' 124.56M -> 93.12M. Max RSS -S -c pass 16.3 -> 17.3 MB, -c pass 17.8 -> 19.3 MB (segment 0 plus whole-page segments; copying the whole image eagerly was +9 MB). Gates: 38 kernel programs (fork, gc, hash_seed, imports, code, site_startup, new code_image_strings) plain/jit/jit-deopt pass (trace_opcodes_threads 'oracle varies' in jit-deopt, oracle-side); check-embed PASS; capi-so alloc_hooks alloc_setget preload_threads static_tp_dict watchers refcount PASS; kernelcheck --checks 31/31; check-harness OK; check-globals flags only os_sched_methods, same on base. Open: stage 2 (heap image) and 3 per the design; mark_kernel's rebound scan (~1M) could come from the image.

**2026-11-21T14:46:34Z**

Review of 526804bc and e510e8f1 (11-21). Defects: (1) lu_intern inserted into interp->interned without the table's lock (pre-existing for runtime strs; the image added a second unlocked insert): 8 threads interning equal strs got up to 1913 of 2000 distinct objects; fixed in 96fe020c (miss path under the table's critical section, entry immortal before publication), new kernel test intern_threads. (2) e510e8f1's fork_signal_hooks timed its interrupt from the holder thread's start, failing 3 of 4 runs in the jit lane; fixed in 91107fe0 (warm fork, clock starts when the main thread is about to fork; 15/15 across plain/jit/jit-deopt). (3) Image code objects and const tuples are invisible to gc.get_objects/get_referrers/is_tracked (CPython lists them): luc-0vrh. Checked and sound: segment first-use copy (acquire check, PyMutex detaching wait, recheck, release publish; the holder never detaches inside load, so neither a collection's lu_image_each_code nor a fork's stop-the-world can see a half copy or inherit the lock held); str/bytes/tuple lazy hashes (relaxed atomic store of one value); immortal headers match lu_set_immortal, so increfs from many threads write nothing; code caches, co_extra, monitoring and JIT state (co_executors) are cleared by imagegen and written only in the private copy; weakrefs to image code work and never fire; frozensets rebuilt per load are immortal heap objects; old cycles' copies stay mapped across Finalize. e510e8f1 matches CPython's PyOS_AfterFork_Parent order and messages for before/after_in_parent/after_in_child hooks (oracle diff identical). Stress (8 threads, 6-10 runs x plain/jit-always/gc:50): concurrent frozen imports, _imp.get_frozen_object, hashing of image strs, specialization of image code: all clean. Noted, not fixed: each Initialize/Finalize cycle maps a new 7.2 MB area and keeps it (1.7 MB touched at boot). -S -c pass after the intern fix: 93.26M instructions (stage 1 recorded 93.02M). Stage 2 (heap image) is off this lane by the coordinator's call.
