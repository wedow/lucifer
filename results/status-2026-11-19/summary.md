# Lucifer status 2026-11-19

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1774.6 | 39.3 | 322.7 | 0.182 | 8.209 | -19.3% |
| CD | 2658.8 | 261.4 | 1419.9 | 0.534 | 5.433 | -17.9% |
| DeltaBlue | 687.0 | 77.3 | 619.2 | 0.901 | 8.012 | -21.4% |
| Havlak | 3693.0 | 453.8 | 2574.5 | 0.697 | 5.673 | -18.6% |
| Json | 904.8 | 203.7 | 1148.4 | 1.269 | 5.639 | -24.9% |
| List | 1126.5 | 249.7 | 613.6 | 0.545 | 2.458 | -13.2% |
| Mandelbrot | 1411.0 | 52.9 | 75.5 | 0.054 | 1.426 | -13.9% |
| NBody | 2008.2 | 64.9 | 477.4 | 0.238 | 7.354 | -4.1% |
| Permute | 2139.7 | 146.7 | 1009.8 | 0.472 | 6.885 | -19.5% |
| Queens | 1053.1 | 86.9 | 367.7 | 0.349 | 4.230 | -18.5% |
| Richards | 4406.9 | 193.5 | 1552.1 | 0.352 | 8.021 | -48.7% |
| Sieve | 2215.5 | 122.6 | 688.1 | 0.311 | 5.612 | +5.0% |
| Storage | 1779.4 | 248.8 | 1315.7 | 0.739 | 5.288 | -12.1% |
| Towers | 1434.1 | 191.9 | 523.2 | 0.365 | 2.727 | -23.0% |

Geomean JIT on / CPython: 0.399 (yesterday 0.503); JIT on / PyPy: 4.967

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2977 | 2469 | 541 | 50.45 | 7944.68 | 0.219 | 1.00 | 1.00 | 1.00 | +6.1% |
| 2 | 2778 | 4018 | 918 | 73.03 | 277.19 | 0.228 | 0.47 | 0.81 | 0.85 | +4.5% |
| 4 | 2767 | 7527 | 1863 | 34.60 | 90.27 | 0.248 | 0.23 | 0.76 | 0.86 | +15.5% |
| 8 | 2315 | 10349 | - | - | - | - | 0.10 | 0.52 | - | - |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.399 | 0.503 | 0 | 0.080 | 4.967 |
| cadence | 5 | 0 | 2.481 | 2.763 | 5 | - | - |
| cli | 6 | 0 | 5.742 | 6.890 | 0 | 2.436 | 2.357 |
| pyperformance | 94 | 0 | 2.366 | 2.669 | 4 | 0.587 | 3.964 |
| pyperformance-warm | 94 | 1 | 2.262 | 2.525 | 4 | 0.491 | 4.563 |
| startup | 3 | 0 | 2.885 | 3.447 | 0 | 1.786 | 1.616 |
| web | 1 | 1 | - | 4.366 | not run | - | - |
| all | 123 | 1 | 2.029 | 2.337 | 9 | 0.509 | 3.872 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | FAIL |  |
| check-capi | pass |  |
| check-kernel | FAIL | FAIL threads_weakset_churn; 778/779 checks pass (plain) |
| check-kernel-jit | pass | 779/779 checks pass (jit); 779/779 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 778 | 763 |
| kernel.plain.total | 779 | 763 |
| kernel.jit.pass | 779 | 763 |
| kernel.jit.total | 779 | 763 |
| kernel.jit-deopt.pass | 779 | 763 |
| kernel.jit-deopt.total | 779 | 763 |
| difftest.always.pass | 193 | - |
| difftest.always.total | 193 | - |
| difftest.deopt.pass | 193 | - |
| difftest.deopt.total | 193 | - |
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

- gate check-globals failed
- gate check-kernel failed
- corpus wrong: pyperformance-warm.sqlalchemy_declarative
