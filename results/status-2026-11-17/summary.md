# Lucifer status 2026-11-17

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1747.2 | 399.7 | 0.229 | +3.4% |
| CD | 2635.4 | 1728.4 | 0.656 | +1.9% |
| DeltaBlue | 670.7 | 787.3 | 1.174 | +6.1% |
| Havlak | 3617.6 | 3164.1 | 0.875 | -13.7% |
| Json | 888.8 | 1529.7 | 1.721 | +6.0% |
| List | 1081.0 | 706.6 | 0.654 | -3.3% |
| Mandelbrot | 1362.3 | 87.7 | 0.064 | -1.6% |
| NBody | 1948.7 | 497.8 | 0.255 | +0.8% |
| Permute | 2091.9 | 1254.7 | 0.600 | +1.2% |
| Queens | 991.7 | 451.4 | 0.455 | -4.4% |
| Richards | 4313.8 | 3024.1 | 0.701 | -4.6% |
| Sieve | 2169.2 | 655.5 | 0.302 | -1.1% |
| Storage | 1725.8 | 1496.7 | 0.867 | +5.7% |
| Towers | 1470.7 | 679.5 | 0.462 | -0.0% |

Geomean JIT on / CPython: 0.503 (yesterday 0.505)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2915 | 2519 | 510 | 48.48 | 7661.59 | 0.202 | 1.00 | 1.00 | 1.00 | +3.3% |
| 2 | 2800 | 4186 | 878 | 83.48 | 122.02 | 0.210 | 0.48 | 0.83 | 0.86 | +5.0% |
| 4 | 2836 | 7523 | 1613 | 42.40 | 138.15 | 0.214 | 0.24 | 0.75 | 0.79 | +3.6% |
| 8 | 2350 | 10366 | 2374 | 27.05 | 87.53 | 0.229 | 0.10 | 0.51 | 0.58 | +5.2% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.503 | 0.505 |
| cadence | 5 | 0 | 2.763 | 2.760 |
| cli | 6 | 0 | 6.890 | 6.879 |
| pyperformance | 94 | 1 | 2.669 | 2.858 |
| pyperformance-warm | 94 | 3 | 2.525 | - |
| startup | 3 | 0 | 3.447 | - |
| web | 1 | 0 | 4.366 | 4.594 |
| all | 123 | 1 | 2.337 | 2.452 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | FAIL |  |
| check-capi | pass |  |
| check-kernel | pass | 763/763 checks pass (plain) |
| check-kernel-jit | pass | 763/763 checks pass (jit); 763/763 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | FAIL | FAIL cmethods: passes, and is still listed in NOT_BUILT; FAIL useriter: passes, and is still listed in NOT_BUILT; 2 failing |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 763 | 748 |
| kernel.plain.total | 763 | 748 |
| kernel.jit.pass | 763 | 748 |
| kernel.jit.total | 763 | 748 |
| kernel.jit-deopt.pass | 763 | 748 |
| kernel.jit-deopt.total | 763 | 748 |
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
- gate check-jit failed
- web cpython-3.14t at 1 CPUs: 1 errors
- corpus crash: pyperformance.html5lib, pyperformance-warm.dulwich_log, pyperformance-warm.html5lib
- corpus wrong: pyperformance-warm.sqlalchemy_declarative
