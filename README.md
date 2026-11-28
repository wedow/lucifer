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
| Pure-Python compute vs Go / PyPy | Go 9.92× (9–10.6×), 14/14<br>PyPy 3.21× (2.91–3.4×), 14/14 | Go 0 failed<br>PyPy 0 failed | 2026-11-28 |
| Held-out real Python programs vs CPython 3.14t | stock 1.03× (0.925–1.15×), 35/35 | stock 0 failed | 2026-11-28 |
| Flask throughput and application time vs CPython 3.14t | 1 CPU 0.268× (0.249–0.288×), 1/1<br>8 CPUs 0.399× (0.39–0.408×), 1/1<br>applications (time) 2.21× (1.98–2.48×), 9/9 | 1 CPU 0 failed<br>8 CPUs 0 failed<br>applications (time) 0 failed | 2026-11-28 |
| Short programs (CLI, startup) vs CPython 3.14t | CLI 3.87× (3.56–4.22×), 6/6<br>startup 2.47× (2.07–2.96×), 3/3<br>fresh process 5.13× (4.68–5.64×), 1/1 | CLI 0 failed<br>startup 0 failed<br>fresh process 0 failed | 2026-11-28 |
| Native-heavy programs vs CPython 3.14t | majority 2.41× (2.16–2.68×), 21/21<br>mixed 1.96× (1.76–2.18×), 28/28<br>boundary 3.21× (2.96–3.49×), 2/2<br>unclassified —, 0/1 | majority 0 failed<br>mixed 0 failed<br>boundary 0 failed<br>unclassified 1 failed | 2026-11-28 |
| JIT churn after warm-up | Lucifer FAIL 92/111 with churn<br>PyPy FAIL 76/107 with churn | Lucifer 1 unmeasured<br>PyPy 5 unmeasured | 2026-11-28 |
| All compute programs (time) | stock 0.687× (0.622–0.753×), 49/49<br>PyPy 3.66× (3.17–4.21×), 48/49<br>Go 9.92× (9–10.6×), 14/14 | stock 0 failed<br>PyPy 1 failed<br>Go 0 failed | 2026-11-28 |
| All application paths (time per operation or request) | stock 2.35× (2.12–2.6×), 11/11<br>PyPy 3.52× (3.02–4.13×), 10/11 | stock 0 failed<br>PyPy 1 failed | 2026-11-28 |
| All short programs (time) | stock 3.48× (3.11–3.9×), 10/10<br>PyPy 1.6× (1.47–1.73×), 10/10 | stock 0 failed<br>PyPy 0 failed | 2026-11-28 |
| All native and mixed programs (time) | stock 2.17× (1.95–2.42×), 51/52<br>PyPy 2.42× (2.04–inf×), 49/52 | stock 1 failed<br>PyPy 3 failed | 2026-11-28 |
| C-API functions implemented | 70.8% | 1,153 of 1,628 | 2026-11-28 |
| numpy test suite passing | 97.1% | 50,745 of 52,273 collected | 2026-11-15 |
| `Lib/test` passing, relative to CPython | 76.9% | 39,092 of CPython's 50,816 | 2026-11-28 |
| Kernel programs passing (worst measured mode) | 100.0% | 972 of 972, 3 modes (worst lane) | 2026-11-28 |
| Differential programs matching CPython (worst measured JIT mode) | 100.0% | 193 of 193, 2 modes (worst lane) | 2026-11-28 |
<!-- stats:end -->

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
