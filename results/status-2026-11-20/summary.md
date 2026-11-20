# Lucifer status 2026-11-20

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1774.6 | 39.3 | 320.8 | 0.181 | 8.159 | -0.6% |
| CD | 2658.8 | 261.4 | 1420.1 | 0.534 | 5.433 | +0.0% |
| DeltaBlue | 687.0 | 77.3 | 641.1 | 0.933 | 8.295 | +3.5% |
| Havlak | 3693.0 | 453.8 | 2681.2 | 0.726 | 5.908 | +4.1% |
| Json | 904.8 | 203.7 | 530.6 | 0.586 | 2.605 | -53.8% |
| List | 1126.5 | 249.7 | 618.8 | 0.549 | 2.479 | +0.8% |
| Mandelbrot | 1411.0 | 52.9 | 76.7 | 0.054 | 1.449 | +1.6% |
| NBody | 2008.2 | 64.9 | 479.6 | 0.239 | 7.387 | +0.5% |
| Permute | 2139.7 | 146.7 | 1027.6 | 0.480 | 7.006 | +1.8% |
| Queens | 1053.1 | 86.9 | 375.6 | 0.357 | 4.321 | +2.1% |
| Richards | 4406.9 | 193.5 | 1555.4 | 0.353 | 8.039 | +0.2% |
| Sieve | 2215.5 | 122.6 | 643.7 | 0.291 | 5.250 | -6.5% |
| Storage | 1779.4 | 248.8 | 1279.5 | 0.719 | 5.142 | -2.8% |
| Towers | 1434.1 | 191.9 | 522.0 | 0.364 | 2.720 | -0.2% |

Geomean JIT on / CPython: 0.379 (yesterday 0.399); JIT on / PyPy: 4.715

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2977 | 2469 | 538 | 45.24 | 7664.97 | 0.218 | 1.00 | 1.00 | 1.00 | -0.4% |
| 2 | 2778 | 4018 | 968 | 69.86 | 556.50 | 0.241 | 0.47 | 0.81 | 0.90 | +5.5% |
| 4 | 2767 | 7527 | 1903 | 33.95 | 197.38 | 0.253 | 0.23 | 0.76 | 0.88 | +2.1% |
| 8 | 2315 | 10349 | 2751 | 26.70 | 62.91 | 0.266 | 0.10 | 0.52 | 0.64 | - |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.379 | 0.399 | 0 | 0.080 | 4.715 |
| cadence | 5 | 0 | 2.481 | 2.481 | 5 | - | - |
| cli | 6 | 0 | 5.440 | 5.742 | 0 | 2.436 | 2.233 |
| pyperformance | 94 | 0 | 2.171 | 2.366 | 4 | 0.587 | 3.634 |
| pyperformance-warm | 94 | 1 | 2.067 | 2.262 | 4 | 0.491 | 4.174 |
| startup | 3 | 0 | 2.847 | 2.885 | 0 | 1.786 | 1.594 |
| web | 1 | 0 | 3.762 | - | not run | - | - |
| all | 123 | 0 | 1.893 | 2.029 | 9 | 0.509 | 3.578 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 793/793 checks pass (plain) |
| check-kernel-jit | FAIL | FAIL trace_opcodes_threads; 792/793 checks pass (jit) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 793 | 778 |
| kernel.plain.total | 793 | 779 |
| kernel.jit.pass | 792 | 779 |
| kernel.jit.total | 793 | 779 |
| difftest.always.pass | 193 | 193 |
| difftest.always.total | 193 | 193 |
| difftest.deopt.pass | 193 | 193 |
| difftest.deopt.total | 193 | 193 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 35857 | 35857 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50745 | 50745 |
| numpytest.date | 2026-11-15 | 2026-11-15 |
| libtest.date | 2026-11-09 | 2026-11-09 |
| capi.implemented | 1152 | 1152 |
| capi.stubs | 476 | 476 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

- gate check-kernel-jit failed
- web lucifer at 1 CPUs: 1 errors
- corpus wrong: pyperformance-warm.sqlalchemy_declarative
