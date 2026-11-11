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
| Speed vs CPython 3.14t, composite across benchmark suites | 0.43x CPython's speed | geomean of 122 programs, 0 fail, 2026-11-11 |
| C-API functions implemented | 70.5% | 1,148 of 1,628 |
| numpy test suite passing | 97.0% | 50,714 of 52,273 collected, run 2026-11-08 |
| `Lib/test` passing, relative to CPython | 70.6% | 35,857 of CPython's 50,816, run 2026-11-09 |
| Kernel programs passing in every mode | 100.0% | 694 of 694, 3 modes |
| Differential programs matching CPython in every JIT mode | 100.0% | 191 of 191, 2 modes |
<!-- stats:end -->

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
