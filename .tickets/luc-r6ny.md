---
id: luc-r6ny
status: open
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

