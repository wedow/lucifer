# Lucifer status 2026-11-21

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1774.6 | 39.3 | 329.1 | 0.185 | 8.370 | +2.6% |
| CD | 2658.8 | 261.4 | 1517.7 | 0.571 | 5.807 | +6.9% |
| DeltaBlue | 687.0 | 77.3 | 646.1 | 0.940 | 8.360 | +0.8% |
| Havlak | 3693.0 | 453.8 | 2960.4 | 0.802 | 6.523 | +10.4% |
| Json | 904.8 | 203.7 | 548.3 | 0.606 | 2.692 | +3.3% |
| List | 1126.5 | 249.7 | 616.6 | 0.547 | 2.470 | -0.4% |
| Mandelbrot | 1411.0 | 52.9 | 75.7 | 0.054 | 1.429 | -1.4% |
| NBody | 2008.2 | 64.9 | 500.4 | 0.249 | 7.708 | +4.3% |
| Permute | 2139.7 | 146.7 | 1032.0 | 0.482 | 7.036 | +0.4% |
| Queens | 1053.1 | 86.9 | 389.2 | 0.370 | 4.477 | +3.6% |
| Richards | 4406.9 | 193.5 | 1614.0 | 0.366 | 8.342 | +3.8% |
| Sieve | 2215.5 | 122.6 | 644.6 | 0.291 | 5.257 | +0.1% |
| Storage | 1779.4 | 248.8 | 1314.3 | 0.739 | 5.282 | +2.7% |
| Towers | 1434.1 | 191.9 | 534.7 | 0.373 | 2.786 | +2.4% |

Geomean JIT on / CPython: 0.390 (yesterday 0.379); JIT on / PyPy: 4.847

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2977 | 2469 | 538 | 45.39 | 7757.28 | 0.218 | 1.00 | 1.00 | 1.00 | -0.0% |
| 2 | 2778 | 4018 | 918 | 79.97 | 132.74 | 0.228 | 0.47 | 0.81 | 0.85 | -5.2% |
| 4 | 2767 | 7527 | 1834 | 34.44 | 72.97 | 0.244 | 0.23 | 0.76 | 0.85 | -3.7% |
| 8 | 2315 | 10349 | 2908 | 23.26 | 89.44 | 0.281 | 0.10 | 0.52 | 0.68 | +5.7% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.390 | 0.379 | 0 | 0.080 | 4.847 |
| cadence | 5 | 0 | 2.093 | 2.481 | 5 | - | - |
| cli | 6 | 0 | 5.447 | 5.440 | 0 | 2.436 | 2.236 |
| pyperformance | 94 | 0 | 2.220 | 2.171 | 4 | 0.587 | 3.719 |
| pyperformance-warm | 94 | 0 | 2.099 | 2.067 | 4 | 0.491 | 4.217 |
| startup | 3 | 0 | 3.353 | 2.847 | 0 | 1.786 | 1.878 |
| web | 1 | 0 | 3.558 | 3.762 | not run | - | - |
| all | 123 | 0 | 1.925 | 1.893 | 9 | 0.509 | 3.673 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | FAIL | FAIL given_matches_run |
| check-kernel | FAIL | FAIL fork_threads; FAIL trace_opcodes_threads; 804/806 checks pass (plain) |
| check-kernel-jit | FAIL | FAIL threads_subclasses; FAIL trace_opcodes_threads; 804/806 checks pass (jit) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 804 | 793 |
| kernel.plain.total | 806 | 793 |
| kernel.jit.pass | 804 | 792 |
| kernel.jit.total | 806 | 793 |
| difftest.always.pass | 193 | 193 |
| difftest.always.total | 193 | 193 |
| difftest.deopt.pass | 193 | 193 |
| difftest.deopt.total | 193 | 193 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 40528 | 35857 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50745 | 50745 |
| numpytest.date | 2026-11-15 | 2026-11-15 |
| libtest.date | 2026-11-21 | 2026-11-09 |
| capi.implemented | 1152 | 1152 |
| capi.stubs | 476 | 476 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

- gate check-capi failed
- gate check-kernel failed
- gate check-kernel-jit failed
