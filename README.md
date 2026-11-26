# Lucifer

A CPython-compatible Python with a JIT and true multicore parallelism, built on lessons from a Smalltalk VM with template and optimizing tiers, deoptimization, type feedback, flat object layout and a many-core scheduler.

The bar:
- numpy's own test suite passes against an unmodified numpy build, with the full C-API (non-limited) from day one.
- No GIL. Production Python behaves correctly, not just toy programs.
- The standard library, built-in types included, is mostly Python compiled by the JIT over a small set of native primitives.
- Every speed claim is measured against CPython, PyPy and free-threaded CPython, across a broad corpus, never one benchmark.

The daily scorecards compare Lucifer with the named reference runtimes. Lower time ratios are faster. The Flask row measures requests per second, where higher is faster.

<!-- stats:begin -->
| Metric | Score | Counts | Date |
|---|---|---|---|
| Pure-Python compute vs Go / PyPy | Go 1.95×, PyPy 1.01× | Go 2/14, PyPy 1/14 | 2026-11-26 |
| Held-out real Python programs vs CPython 3.14t | stock 0.821× | stock 4/37 | 2026-11-26 |
| Web (Flask, 1 and 8 CPUs) vs CPython 3.14t | 1 CPU 0.267×, 8 CPUs — | 1 CPU 1/1, 8 CPUs 0/1 | 2026-11-26 |
| Short programs (CLI, startup) vs CPython 3.14t | CLI 3.89×, startup 2.16× | CLI 6/6, startup 3/3 | 2026-11-26 |
| Native-heavy programs vs CPython 3.14t | majority 2.26×, mixed 1.95× | majority 20/20, mixed 27/27 | 2026-11-26 |
| Programs that reach steady state | 24/77 settled | 52 unsettled, 1 unavailable | 2026-11-26 |
| C-API functions implemented | 70.8% | 1,153 of 1,628 | 2026-11-26 |
| numpy test suite passing | 97.1% | 50,745 of 52,273 collected | 2026-11-15 |
| `Lib/test` passing, relative to CPython | 79.8% | 40,528 of CPython's 50,816 | 2026-11-21 |
| Kernel programs passing (worst measured mode) | 99.9% | 970 of 971, 2 modes recorded | 2026-11-26 |
| Differential programs matching CPython (worst measured JIT mode) | 100.0% | 193 of 193, 2 modes | 2026-11-26 |
<!-- stats:end -->

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
