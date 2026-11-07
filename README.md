# Lucifer

A CPython-compatible Python with a JIT and true multicore parallelism, built on lessons from a Smalltalk VM with template and optimizing tiers, deoptimization, type feedback, flat object layout and a many-core scheduler.

The bar:
- numpy's own test suite passes against an unmodified numpy build, with the full C-API (non-limited) from day one.
- No GIL. Production Python behaves correctly, not just toy programs.
- The standard library, built-in types included, is mostly Python compiled by the JIT over a small set of native primitives.
- Every speed claim is measured against CPython, PyPy and free-threaded CPython, across a broad corpus, never one benchmark.

Status, refreshed by each daily status run (details: `tools/scoreboard`):

<!-- stats:begin -->
| Metric | Score | Counts |
|---|---|---|
| Speed vs CPython 3.14t, composite across benchmark suites | 1.05x CPython's speed | geomean of 15 programs, 2026-11-07 |
| C-API functions implemented | 66.5% | 1,083 of 1,628 |
| numpy test suite passing | 96.2% | 50,299 of 52,273 collected, run 2026-11-05 |
| Core-type `Lib/test` passing, relative to CPython | 92.4% | 1,993 of CPython's 2,157, run 2026-11-04 |
| Kernel programs passing in every mode | 100.0% | 593 of 593, 3 modes |
| Differential programs matching CPython in every JIT mode | 100.0% | 164 of 164, 2 modes |
<!-- stats:end -->

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
