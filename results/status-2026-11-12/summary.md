# Lucifer status 2026-11-12

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1737.6 | 584.1 | 0.336 | -1.6% |
| CD | 2588.9 | 921.6 | 0.356 | +1.7% |
| DeltaBlue | 642.3 | 403.0 | 0.627 | -2.9% |
| Havlak | 3485.0 | 2259.5 | 0.648 | -7.1% |
| Json | 906.7 | 860.1 | 0.949 | -0.2% |
| List | 1085.3 | 606.8 | 0.559 | -1.5% |
| Mandelbrot | 1374.9 | 93.1 | 0.068 | +0.8% |
| NBody | 1948.3 | 336.8 | 0.173 | +0.0% |
| Permute | 2108.4 | 721.3 | 0.342 | -1.7% |
| Queens | 1002.2 | 307.2 | 0.307 | -0.7% |
| Richards | 4304.2 | 1898.1 | 0.441 | +1.2% |
| Sieve | 2169.2 | 615.2 | 0.284 | +0.2% |
| Storage | 1760.8 | 1149.3 | 0.653 | +0.2% |
| Towers | 1413.7 | 504.6 | 0.357 | +0.3% |

Geomean JIT on / CPython: 0.371 (yesterday 0.374)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3099 | 2579 | 717 | 38.40 | 7917.27 | 0.278 | 1.00 | 1.00 | 1.00 | +8.2% |
| 2 | 2870 | 4333 | 1213 | 57.30 | 76.80 | 0.280 | 0.46 | 0.84 | 0.85 | +19.0% |
| 4 | 2593 | 8336 | 2399 | 28.74 | 79.77 | 0.288 | 0.21 | 0.81 | 0.84 | +39.9% |
| 8 | 2139 | 10658 | 3426 | 20.13 | 49.03 | 0.321 | 0.09 | 0.52 | 0.60 | +8.2% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.371 | 0.374 |
| cadence | 5 | 0 | 2.755 | 2.795 |
| cli | 6 | 0 | 5.640 | 6.398 |
| pyperformance | 96 | 0 | 2.475 | 2.847 |
| web | 1 | 0 | 3.111 | 3.367 |
| all | 122 | 0 | 2.086 | 2.348 |

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 711/711 checks pass (plain) |
| check-kernel-jit | pass | 711/711 checks pass (jit); 711/711 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 711 | 694 |
| kernel.plain.total | 711 | 694 |
| kernel.jit.pass | 711 | 694 |
| kernel.jit.total | 711 | 694 |
| kernel.jit-deopt.pass | 711 | 694 |
| kernel.jit-deopt.total | 711 | 694 |
| difftest.always.pass | 193 | 191 |
| difftest.always.total | 193 | 191 |
| difftest.deopt.pass | 193 | 191 |
| difftest.deopt.total | 193 | 191 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 35857 | 35857 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50714 | 50714 |
| numpytest.date | 2026-11-08 | 2026-11-08 |
| libtest.date | 2026-11-09 | 2026-11-09 |
| capi.implemented | 1148 | 1148 |
| capi.stubs | 480 | 480 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

none
