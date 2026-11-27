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
| Pure-Python compute vs Go / PyPy | Go 10.7× (10–11.3×), 14/14<br>PyPy 3.3× (3.06–3.44×), 14/14 | Go 0 failed<br>PyPy 0 failed | 2026-11-27 |
| Held-out real Python programs vs CPython 3.14t | stock 1.03× (0.937–1.13×), 36/36 | stock 0 failed | 2026-11-27 |
| Flask throughput and application time vs CPython 3.14t | 1 CPU 0.266× (0.246–0.285×), 1/1<br>8 CPUs —, 0/1<br>applications (time) 2.12× (1.94–2.32×), 9/9 | 1 CPU 0 failed<br>8 CPUs 1 failed<br>applications (time) 0 failed | 2026-11-27 |
| Short programs (CLI, startup) vs CPython 3.14t | CLI 4.38× (4.1–4.67×), 6/6<br>startup 2.26× (2.01–2.54×), 3/3<br>fresh process 5.5× (4.23–7.22×), 1/1 | CLI 0 failed<br>startup 0 failed<br>fresh process 0 failed | 2026-11-27 |
| Native-heavy programs vs CPython 3.14t | majority 2.25× (2.07–2.44×), 20/20<br>mixed 2× (1.82–2.19×), 28/28<br>boundary 3.49× (3.19–3.82×), 2/2<br>unclassified —, 0/1 | majority 0 failed<br>mixed 0 failed<br>boundary 0 failed<br>unclassified 1 failed | 2026-11-27 |
| JIT churn after warm-up | Lucifer FAIL 92/111 with churn<br>PyPy FAIL 76/107 with churn | Lucifer 2 unmeasured<br>PyPy 6 unmeasured | 2026-11-27 |
| All compute programs (time) | stock 0.694× (0.639–0.754×), 50/50<br>PyPy 3.87× (3.42–4.39×), 49/50<br>Go 10.7× (10–11.3×), 14/14 | stock 0 failed<br>PyPy 1 failed<br>Go 0 failed | 2026-11-27 |
| All application paths (time per operation or request) | stock 2.25× (2.05–2.45×), 10/11<br>PyPy 4.11× (3.44–4.98×), 9/11 | stock 1 failed<br>PyPy 2 failed | 2026-11-27 |
| All short programs (time) | stock 3.67× (3.32–4.06×), 10/10<br>PyPy 1.56× (1.42–1.7×), 10/10 | stock 0 failed<br>PyPy 0 failed | 2026-11-27 |
| All native and mixed programs (time) | stock 2.14× (1.96–2.34×), 50/51<br>PyPy 2.39× (2.05–inf×), 48/51 | stock 1 failed<br>PyPy 3 failed | 2026-11-27 |
| C-API functions implemented | 70.8% | 1,153 of 1,628 | 2026-11-27 |
| numpy test suite passing | 97.1% | 50,745 of 52,273 collected | 2026-11-15 |
| `Lib/test` passing, relative to CPython | 79.8% | 40,528 of CPython's 50,816 | 2026-11-21 |
| Kernel programs passing (worst measured mode) | 100.0% | 972 of 972, 3 modes (worst lane) | 2026-11-27 |
| Differential programs matching CPython (worst measured JIT mode) | 100.0% | 193 of 193, 2 modes (worst lane) | 2026-11-27 |
<!-- stats:end -->

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
