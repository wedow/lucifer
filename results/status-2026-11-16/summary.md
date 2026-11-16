# Lucifer status 2026-11-16

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1747.2 | 386.4 | 0.221 | -0.3% |
| CD | 2635.4 | 1696.4 | 0.644 | -19.6% |
| DeltaBlue | 670.7 | 741.9 | 1.106 | -39.2% |
| Havlak | 3617.6 | 3667.6 | 1.014 | -14.9% |
| Json | 888.8 | 1443.5 | 1.624 | -8.8% |
| List | 1081.0 | 730.6 | 0.676 | -5.2% |
| Mandelbrot | 1362.3 | 89.1 | 0.065 | -2.0% |
| NBody | 1948.7 | 494.0 | 0.254 | -5.1% |
| Permute | 2091.9 | 1239.7 | 0.593 | -3.3% |
| Queens | 991.7 | 472.3 | 0.476 | +0.2% |
| Richards | 4313.8 | 3168.9 | 0.735 | -3.7% |
| Sieve | 2169.2 | 662.5 | 0.305 | +1.2% |
| Storage | 1725.8 | 1416.6 | 0.821 | -5.7% |
| Towers | 1470.7 | 679.5 | 0.462 | +1.8% |

Geomean JIT on / CPython: 0.505 (yesterday 0.553)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2915 | 2519 | 493 | 47.22 | 7855.44 | 0.196 | 1.00 | 1.00 | 1.00 | -10.0% |
| 2 | 2800 | 4186 | 836 | 86.75 | 140.39 | 0.200 | 0.48 | 0.83 | 0.85 | -12.0% |
| 4 | 2836 | 7523 | 1557 | 44.11 | 183.56 | 0.207 | 0.24 | 0.75 | 0.79 | -12.0% |
| 8 | 2350 | 10366 | 2257 | 28.45 | 115.83 | 0.218 | 0.10 | 0.51 | 0.57 | -15.7% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.505 | 0.553 |
| cadence | 5 | 0 | 2.760 | 2.983 |
| cli | 6 | 0 | 6.879 | 8.059 |
| pyperformance | 96 | 0 | 2.858 | 3.332 |
| web | 1 | 0 | 4.594 | 3.983 |
| all | 122 | 0 | 2.452 | 2.823 |

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 748/748 checks pass (plain) |
| check-kernel-jit | pass | 748/748 checks pass (jit); 748/748 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 748 | 732 |
| kernel.plain.total | 748 | 732 |
| kernel.jit.pass | 748 | 731 |
| kernel.jit.total | 748 | 732 |
| kernel.jit-deopt.pass | 748 | - |
| kernel.jit-deopt.total | 748 | - |
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

- web cpython-3.14t at 1 CPUs: 1 errors
