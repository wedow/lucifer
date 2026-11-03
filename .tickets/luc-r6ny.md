---
id: luc-r6ny
status: closed
deps: [luc-dfbu]
links: [luc-f56i]
created: 2026-11-03T02:15:01Z
type: feature
priority: 0
parent: luc-nsmh
tags: [runtime, abi, slice]
---
# K0.1: runtime state records, allocator domains, globals audit

Fix the landed K0 conflicts of docs/FOUNDATIONS.md 3 (C1-C4) before K1 builds on them: _PyRuntime as the runtime record (allocator domain tables, lock registry, reserved _Py_DebugOffsets head); PyMemAllocatorEx tables for raw/mem/obj with real PyMem_SetAllocator/GetAllocator; default contexts pick the per-(carrier, interpreter) mimalloc heap by id MEM/OBJECT/GC/GC_PRE; lu_gc_alloc; BRC buckets in PyInterpreterState and the owner record in the carrier entry (no second _Thread_local, no registration in _Py_NewReference); PyRefTracer hook in _Py_NewReference/_Py_Dealloc; -fno-omit-frame-pointer; bin/lucifer links main.o with build/liblucifer.a (FOUNDATIONS 2.1, 2.6, 2.9, 2.12, 2.13).

## Acceptance Criteria

make check-globals: every writable symbol nm reports in bin/lucifer is an exported oracle data symbol, the carrier TLS word, a mimalloc process global or a reasoned row in tools/abi/globals.tsv. make check-capi: tests/capi/alloc_hooks.c wraps all three domains with counting allocators that delegate, sees PyMem_RawMalloc/PyMem_Malloc/PyObject_Malloc counted, restores them, and sees PyRefTracer create/destroy events for a PyObject_Init object released by Py_DECREF. make abi-audit unchanged.


## Notes

**2026-11-03T02:27:59Z**

Done. _PyRuntime is a union at the oracle's 365,624 bytes: reserved _Py_DebugOffsets head (cookie xdebugpy, version, free-threaded flag, 1 KiB total), raw/mem/obj PyMemAllocatorEx tables, ref_tracer, lock registry, carrier pthread key, main interpreter. PyMem_Set/GetAllocator real; default mem/obj hooks use the carrier entry's mimalloc heaps MEM/OBJECT/GC/GC_PRE (page tag = heap id). lu_gc_alloc(tp, size, presize) picks GC_PRE from Py_TPFLAGS_PREHEADER, as CPython's _PyObject_GetAllocationHeap does, not from presize. BRC buckets in struct _is; owner record in struct lu_entry, reached via the single lu_carrier TLS word (runtime/carrier.c); _Py_NewReference no longer registers. PyRefTracer in _Py_NewReference/_Py_Dealloc, slot in _PyRuntime like cp314t. -fno-omit-frame-pointer. bin/lucifer links main.o with build/liblucifer.a (whole-archive: extensions bind to symbols main.o never names). make check-globals + tools/abi/globals.tsv. tests/capi/alloc_hooks.c. Checks: check-globals, check-capi (all three tests, 15 runs each), abi-audit 1843/1843 and 215/215, check-harness all pass. Not done: PyMem_SetupDebugHooks stays a stub; no QSBR, no GC counters in lu_gc_alloc (K1/M0.2); per-interpreter mimalloc subprocess for non-main interpreters arrives with interpreter creation. The pthread_create wrapper stays until thread states bind carriers.
