---
id: luc-3jg0
status: closed
deps: []
links: []
created: 2026-11-09T04:18:28Z
type: task
priority: 1
tags: [corpus, numpy]
---
# cadence: run its test suite and benchmarks on Lucifer

Run cadence-net 0.95.1 (numpy-heavy brain simulation, pinned to tag v0.95.1, commit 7fc55e7) on Lucifer: install with numpy and the optional extras, pass its pytest testpaths as CPython 3.14t does, add 3-6 of its workloads to the corpus as cadence.<name>, and profile where time goes against CPython.

## Acceptance Criteria

Per-test comparison with CPython 3.14t; Lucifer failures fixed in general form or ticketed; cadence.<name> programs in the corpus with output checks and a CPython reference; profile write-up with tickets by mechanism.


## Notes

**Install.** The corpus lock pins the cadence-net 0.95.1 sdist (tag v0.95.1, commit 7fc55e7; the sdist is that commit's tree minus result receipts no test reads, 18 MB against 59 MB for the commit tarball) and numpy 2.5.4's cp314t wheel; `tools/bench/corpus prepare` unpacks the release tree beside the site as build/corpus/cadence-net. Vendoring later: change `cadence_tree()` and drop the lock entry. Optional extras on Lucifer: scipy 1.18.1 imports and its sparse tests pass; numba 0.68.1 compiles and its tests pass (after the ctypes fix below); torch 2.15.0 (CPU cp314t wheel) fails in torch._C's init (luc-o0vt); mlx has no Linux wheel.

**Test suite** (pyproject testpaths, 236 files, numpy only, same flags and PYTHONHASHSEED=0, six file shards per runtime). CPython 3.14t: 3176 pass, 25 fail (recorded checkpoint bytes that differ off the author's machine, documentation links, torch-only cases), 405 skip, 1 xfail. Lucifer before this lane: numpy.random's import crashed, so nearly nothing ran. After the first fix: 3165 pass, 36 fail; the 11 extra failures were 4 mechanisms, all fixed here:
- PyObject_GC_New gave an inline-values instance no room (numpy.random import: heap corruption by Cython functions' attributes).
- Exception inits went through setattr, so PackageNotFoundError's name property broke construction (8 tests).
- numpy scalars copying float's tp_hash were unhashable (1 test; numpy's TestHash float cases too).
- os.abort and os.killpg/getpgid/setpgid/getsid were missing (2 tests).
With all four fixed, Lucifer matches CPython test for test: 3176 pass, the same 25 fail, 405 skip, 1 xfail. With numba and scipy installed, the 12 files that use them: CPython 132 pass, 3 skip; Lucifer the same except tests/test_thread_benchmark.py, which imports cProfile (luc-kc4d).

**Corpus.** Suite cadence: continuing_brain, walking_rhythm, walking_for_reward (the shipped examples, unmodified) and bandit, night_replay (one small cell of the extinction and replay chambers). Output checks are their JSON or tables, which match CPython bit for bit. Quiet-host times, CPython 3.14t / Lucifer (on_over_cpython): continuing_brain 0.79 s / 3.62 s (4.58), walking_rhythm 5.91 / 24.57 (4.16), walking_for_reward 6.20 / 30.56 (4.93), bandit 1.17 / 5.40 (4.62), night_replay 1.83 / 8.55 (4.68); geomean 4.59.

**Profile** (perf with -X perf, OPENBLAS_NUM_THREADS=1; in these runs Lucifer took 2.7-4.5x CPython's time). What is new against the pure-Python corpus is the C-API boundary taken once per numpy operation:
- PyContextVar_Get runs the Python ContextVar.get: 28-40% of Lucifer's time (numpy's memory handler on every allocation, the errstate extobj on every ufunc call), a KeyError and a deopt per miss (luc-r4xh).
- Python operators on ndarrays and numpy scalars go through kernel operators and slot wrappers instead of the C slot: 4-8% (luc-9pfh).
- Number conversions from C (PyNumber_Float from DOUBLE_setitem, int()/float() of scalars) run kernel constructors: 3-5% (luc-wle9).
- numpy's own code takes 1.8-2.5x the cycles it takes under CPython, evenly across its functions (luc-j3va, cause unverified).
The interpreter and JIT side (type_lookup, dict probes, allocation, deopts) is the familiar remainder.
