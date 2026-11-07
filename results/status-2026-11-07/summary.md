# Lucifer status 2026-11-07

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1729.1 | 938.4 | 0.543 | -10.1% |
| CD | 2573.9 | 6906.1 | 2.683 | -16.2% |
| DeltaBlue | 636.6 | 1376.6 | 2.163 | -15.2% |
| Havlak | 3519.8 | 7473.5 | 2.123 | -8.9% |
| Json | 874.4 | 2263.2 | 2.588 | -36.6% |
| List | 1055.9 | 825.6 | 0.782 | -22.6% |
| Mandelbrot | 1368.0 | 357.9 | 0.262 | -23.6% |
| NBody | 1960.5 | 480.7 | 0.245 | -24.9% |
| Permute | 2073.0 | 947.7 | 0.457 | -28.9% |
| Queens | 1003.3 | 484.2 | 0.483 | -25.3% |
| Richards | 4348.4 | 2194.5 | 0.505 | -13.2% |
| Sieve | 2153.1 | 876.5 | 0.407 | -0.9% |
| Storage | 1738.5 | 6245.3 | 3.592 | -33.7% |
| Towers | 1399.0 | 676.3 | 0.483 | -28.7% |

Geomean JIT on / CPython: 0.826 (yesterday 1.048)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3101 | 2601 | 322 | 50.21 | 7763.52 | 0.124 | 1.00 | 1.00 | 1.00 | - |
| 2 | 2867 | 4372 | 545 | 111.50 | 186.92 | 0.125 | 0.46 | 0.84 | 0.85 | - |
| 4 | 2191 | 8110 | 1101 | 57.33 | 72.04 | 0.136 | 0.18 | 0.78 | 0.85 | - |
| 8 | 2160 | 10103 | 1511 | 47.81 | 110.79 | 0.150 | 0.09 | 0.49 | 0.59 | - |

## Gates

| target | result | detail |
|---|---|---|
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 593/593 checks pass (plain) |
| check-kernel-jit | pass | 593/593 checks pass (jit); 593/593 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 593 | 583 |
| kernel.plain.total | 593 | 583 |
| kernel.jit.pass | 593 | 583 |
| kernel.jit.total | 593 | 583 |
| kernel.jit-deopt.pass | 593 | 583 |
| kernel.jit-deopt.total | 593 | 583 |
| difftest.oracle.pass | 164 | 164 |
| difftest.total | 164 | 164 |
| difftest.lucifer.pass | 153 | 153 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 1993 | 1993 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50299 | 50299 |
| capi.implemented | 1083 | 968 |
| capi.stubs | 545 | 660 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

none
