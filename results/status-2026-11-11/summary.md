# Lucifer status 2026-11-11

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1737.6 | 593.4 | 0.342 | +5.9% |
| CD | 2588.9 | 905.8 | 0.350 | -6.6% |
| DeltaBlue | 642.3 | 414.9 | 0.646 | -2.2% |
| Havlak | 3485.0 | 2431.6 | 0.698 | +6.3% |
| Json | 906.7 | 861.7 | 0.950 | -16.0% |
| List | 1085.3 | 616.1 | 0.568 | -0.5% |
| Mandelbrot | 1374.9 | 92.4 | 0.067 | +1.4% |
| NBody | 1948.3 | 336.7 | 0.173 | +2.7% |
| Permute | 2108.4 | 733.5 | 0.348 | +0.1% |
| Queens | 1002.2 | 309.2 | 0.309 | +0.8% |
| Richards | 4304.2 | 1874.8 | 0.436 | +4.5% |
| Sieve | 2169.2 | 614.2 | 0.283 | +0.8% |
| Storage | 1760.8 | 1146.6 | 0.651 | +0.6% |
| Towers | 1413.7 | 503.1 | 0.356 | -1.0% |

Geomean JIT on / CPython: 0.374 (yesterday 0.375)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3099 | 2579 | 662 | 42.01 | 7945.51 | 0.257 | 1.00 | 1.00 | 1.00 | +19.7% |
| 2 | 2870 | 4333 | 1020 | 71.57 | 118.95 | 0.235 | 0.46 | 0.84 | 0.77 | +8.9% |
| 4 | 2593 | 8336 | 1715 | 37.49 | 58.13 | 0.206 | 0.21 | 0.81 | 0.65 | -6.6% |
| 8 | 2139 | 10658 | 3166 | 21.84 | 53.40 | 0.297 | 0.09 | 0.52 | 0.60 | +17.5% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.374 | 0.375 |
| cadence | 5 | 0 | 2.795 | 2.960 |
| cli | 6 | 0 | 6.398 | 6.719 |
| pyperformance | 96 | 0 | 2.847 | 3.650 |
| web | 1 | 0 | 3.367 | 3.955 |
| all | 122 | 0 | 2.348 | 2.868 |

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 694/694 checks pass (plain) |
| check-kernel-jit | pass | 694/694 checks pass (jit); 694/694 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 694 | 666 |
| kernel.plain.total | 694 | 666 |
| kernel.jit.pass | 694 | 666 |
| kernel.jit.total | 694 | 666 |
| kernel.jit-deopt.pass | 694 | 666 |
| kernel.jit-deopt.total | 694 | 666 |
| difftest.always.pass | 191 | 165 |
| difftest.always.total | 191 | 165 |
| difftest.deopt.pass | 191 | 165 |
| difftest.deopt.total | 191 | 165 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 35857 | 35857 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50714 | 50714 |
| numpytest.date | 2026-11-08 | 2026-11-08 |
| libtest.date | 2026-11-09 | 2026-11-09 |
| capi.implemented | 1148 | 1134 |
| capi.stubs | 480 | 494 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

none
