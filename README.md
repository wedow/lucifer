# Lucifer

A CPython-compatible Python with a JIT and true multicore parallelism, built on lessons from a Smalltalk VM with template and optimizing tiers, deoptimization, type feedback, flat object layout and a many-core scheduler.

The bar:
- numpy's own test suite passes against an unmodified numpy build, with the full C-API (non-limited) from day one.
- No GIL. Production Python behaves correctly, not just toy programs.
- The standard library, built-in types included, is mostly Python compiled by the JIT over a small set of native primitives.
- Every speed claim is measured against CPython, PyPy and free-threaded CPython, across a broad corpus, never one benchmark.

The daily scorecards compare Lucifer with the named reference runtimes. Lower time ratios are faster. Higher throughput ratios are faster for the Flask CPU layouts. Each timing score reports its propagated interval and program coverage.

<!-- stats:begin -->
| Metric | Score | Counts | Date |
|---|---|---|---|
| Pure-Python compute vs Go / PyPy | Go 10.7×, PyPy 2.67× | Go 5/14, PyPy 3/14 | 2026-11-27 |
| Held-out real Python programs vs CPython 3.14t | stock 0.813× | stock 9/36 | 2026-11-27 |
| Web (Flask, 1 and 8 CPUs) vs CPython 3.14t | 1 CPU 0.264×, 8 CPUs 0.427× | 1 CPU 1/1, 8 CPUs 1/1 | 2026-11-27 |
| Short programs (CLI, startup) vs CPython 3.14t | CLI 3.98×, startup 2.12× | CLI 6/6, startup 3/3 | 2026-11-27 |
| Native-heavy programs vs CPython 3.14t | majority 2.41×, mixed 1.97× | majority 21/21, mixed 27/27 | 2026-11-27 |
| Programs that reach steady state | 23/75 settled | 52 unsettled, 0 unavailable | 2026-11-27 |
| C-API functions implemented | 70.8% | 1,153 of 1,628 | 2026-11-27 |
| numpy test suite passing | 97.1% | 50,745 of 52,273 collected | 2026-11-15 |
| `Lib/test` passing, relative to CPython | 79.8% | 40,528 of CPython's 50,816 | 2026-11-21 |
| Kernel programs passing (worst measured mode) | 100.0% | 972 of 972, 3 modes (worst lane) | 2026-11-27 |
| Differential programs matching CPython (worst measured JIT mode) | 100.0% | 193 of 193, 2 modes (worst lane) | 2026-11-27 |
<!-- stats:end -->

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
