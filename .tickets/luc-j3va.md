---
id: luc-j3va
status: open
deps: []
links: []
created: 2026-11-09T05:53:29Z
type: task
priority: 3
tags: [perf, numpy]
---
# perf: numpy's own code takes about 1.8-2.5x the cycles under Lucifer

In the walking_rhythm profile (OPENBLAS_NUM_THREADS=1, -X perf) the _multiarray_umath DSO takes 8.5 s under Lucifer against 4.7 s under CPython 3.14t for the same program and output, and the ratio holds across its functions (ufunc_generic_fastcall, NpyIter_AdvancedNew, npyiter_fill_axisdata, PyArray_NewFromDescr_int, about 1.6-2.5x), so it is not a different path through numpy. Unverified causes: cache and branch-predictor pressure from the runtime's larger footprint, inline Py_INCREF/Py_DECREF in numpy taking the shared (atomic) path when ob_tid does not match the reader's _Py_ThreadId, or more calls of the same functions.

## Design

perf stat -e instructions,cycles,cache-misses,branch-misses restricted to the numpy DSO (perf record --dso, or uprobes call counts on ufunc_generic_fastcall and PyArray_NewFromDescr_int) under both runtimes; check the ob_tid of objects numpy receives against fs:0.

