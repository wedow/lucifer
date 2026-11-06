# Lucifer status 2026-11-06

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | JIT off | on/CPython | on vs yesterday |
|---|---|---|---|---|---|
| Bounce | 1729.1 | 1044.2 | 4468.3 | 0.604 | -74.7% |
| CD | 2573.9 | 8245.0 | 66062.7 | 3.203 | -74.2% |
| DeltaBlue | 636.6 | 1623.3 | 2080.0 | 2.550 | -76.6% |
| Havlak | 3519.8 | 8208.1 | 11725.7 | 2.332 | -56.2% |
| Json | 874.4 | 3571.2 | 4774.6 | 4.084 | -39.5% |
| List | 1055.9 | 1067.2 | 2031.3 | 1.011 | -74.2% |
| Mandelbrot | 1368.0 | 468.7 | 145148.2 | 0.343 | -25.1% |
| NBody | 1960.5 | 639.8 | 170408.9 | 0.326 | -68.9% |
| Permute | 2073.0 | 1332.3 | 6694.2 | 0.643 | -42.9% |
| Queens | 1003.3 | 648.2 | 2623.4 | 0.646 | -29.2% |
| Richards | 4348.4 | 2529.4 | 10388.7 | 0.582 | -74.5% |
| Sieve | 2153.1 | 884.7 | 4690.8 | 0.411 | -59.3% |
| Storage | 1738.5 | 9424.4 | 21334.9 | 5.421 | -51.5% |
| Towers | 1399.0 | 947.9 | 3058.8 | 0.678 | -51.1% |

Geomean JIT on / CPython: 1.048 (yesterday 2.640)
Geomean JIT on / JIT off: 0.174
JIT on slower than off: none

## Gates

| target | result | detail |
|---|---|---|
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 583/583 checks pass (plain) |
| check-kernel-jit | pass | 583/583 checks pass (jit); 583/583 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 583 | 550 |
| kernel.plain.total | 583 | 550 |
| kernel.jit.pass | 583 | 549 |
| kernel.jit.total | 583 | 550 |
| kernel.jit-deopt.pass | 583 | - |
| kernel.jit-deopt.total | 583 | - |
| difftest.oracle.pass | 164 | 164 |
| difftest.total | 164 | 164 |
| difftest.lucifer.pass | 153 | 153 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 1993 | 1993 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50299 | 2184 |
| capi.implemented | 968 | 934 |
| capi.stubs | 660 | 694 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

none
