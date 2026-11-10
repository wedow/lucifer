# Lucifer status 2026-11-10

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1737.6 | 560.4 | 0.323 | +0.6% |
| CD | 2588.9 | 969.8 | 0.375 | -2.2% |
| DeltaBlue | 642.3 | 424.3 | 0.661 | -7.3% |
| Havlak | 3485.0 | 2287.3 | 0.656 | -5.5% |
| Json | 906.7 | 1026.2 | 1.132 | +0.5% |
| List | 1085.3 | 619.0 | 0.570 | -3.9% |
| Mandelbrot | 1374.9 | 91.1 | 0.066 | -0.2% |
| NBody | 1948.3 | 327.7 | 0.168 | -2.1% |
| Permute | 2108.4 | 732.7 | 0.348 | +1.3% |
| Queens | 1002.2 | 306.8 | 0.306 | -5.4% |
| Richards | 4304.2 | 1794.8 | 0.417 | -8.8% |
| Sieve | 2169.2 | 609.1 | 0.281 | -0.2% |
| Storage | 1760.8 | 1139.4 | 0.647 | -1.9% |
| Towers | 1413.7 | 508.3 | 0.360 | -1.6% |

Geomean JIT on / CPython: 0.375 (yesterday 0.385)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3099 | 2579 | 553 | 51.60 | 7874.13 | 0.215 | 1.00 | 1.00 | 1.00 | +20.6% |
| 2 | 2870 | 4333 | 936 | 82.18 | 113.40 | 0.216 | 0.46 | 0.84 | 0.85 | +22.6% |
| 4 | 2593 | 8336 | 1836 | 41.52 | 68.21 | 0.220 | 0.21 | 0.81 | 0.83 | +21.7% |
| 8 | 2139 | 10658 | 2695 | 24.27 | 56.50 | 0.253 | 0.09 | 0.52 | 0.61 | +29.5% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.375 | 0.385 |
| cadence | 5 | 0 | 2.960 | - |
| cli | 6 | 0 | 6.719 | 7.055 |
| pyperformance | 96 | 1 | 3.650 | 4.187 |
| web | 1 | 0 | 3.955 | 5.120 |
| all | 122 | 1 | 2.868 | 3.238 |

## Gates

| target | result | detail |
|---|---|---|
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 666/666 checks pass (plain) |
| check-kernel-jit | pass | 666/666 checks pass (jit); 666/666 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 666 | 651 |
| kernel.plain.total | 666 | 651 |
| kernel.jit.pass | 666 | 651 |
| kernel.jit.total | 666 | 651 |
| kernel.jit-deopt.pass | 666 | 651 |
| kernel.jit-deopt.total | 666 | 651 |
| difftest.always.pass | 165 | 165 |
| difftest.always.total | 165 | 165 |
| difftest.deopt.pass | 165 | 165 |
| difftest.deopt.total | 165 | 165 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 35857 | 1993 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50714 | 50714 |
| numpytest.date | 2026-11-08 | 2026-11-08 |
| libtest.date | 2026-11-09 | - |
| capi.implemented | 1134 | 1134 |
| capi.stubs | 494 | 494 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

- corpus wrong: pyperformance.docutils
