# Lucifer status 2026-11-15

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1737.6 | 387.5 | 0.223 | -33.7% |
| CD | 2588.9 | 2111.1 | 0.815 | +129.1% |
| DeltaBlue | 642.3 | 1219.8 | 1.899 | +202.7% |
| Havlak | 3485.0 | 4312.2 | 1.237 | +90.8% |
| Json | 906.7 | 1583.3 | 1.746 | +84.1% |
| List | 1085.3 | 771.1 | 0.710 | +27.1% |
| Mandelbrot | 1374.9 | 90.9 | 0.066 | -2.4% |
| NBody | 1948.3 | 520.6 | 0.267 | +54.6% |
| Permute | 2108.4 | 1281.9 | 0.608 | +77.7% |
| Queens | 1002.2 | 471.4 | 0.470 | +53.5% |
| Richards | 4304.2 | 3290.5 | 0.764 | +73.4% |
| Sieve | 2169.2 | 654.9 | 0.302 | +6.5% |
| Storage | 1760.8 | 1502.7 | 0.853 | +30.7% |
| Towers | 1413.7 | 667.4 | 0.472 | +32.3% |

Geomean JIT on / CPython: 0.553 (yesterday 0.371)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3099 | 2579 | 548 | 46.29 | 8060.49 | 0.212 | 1.00 | 1.00 | 1.00 | -23.5% |
| 2 | 2870 | 4333 | 949 | 79.28 | 117.05 | 0.219 | 0.46 | 0.84 | 0.87 | -21.7% |
| 4 | 2593 | 8336 | 1769 | 41.55 | 149.80 | 0.212 | 0.21 | 0.81 | 0.81 | -26.2% |
| 8 | 2139 | 10658 | 2676 | 28.05 | 62.72 | 0.251 | 0.09 | 0.52 | 0.61 | -21.9% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.553 | 0.371 |
| cadence | 5 | 0 | 2.983 | 2.755 |
| cli | 6 | 0 | 8.059 | 5.640 |
| pyperformance | 96 | 0 | 3.332 | 2.475 |
| web | 1 | 0 | 3.983 | 3.111 |
| all | 122 | 0 | 2.823 | 2.086 |

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 732/732 checks pass (plain) |
| check-kernel-jit | FAIL | FAIL threads_cache_churn; 731/732 checks pass (jit) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 732 | 711 |
| kernel.plain.total | 732 | 711 |
| kernel.jit.pass | 731 | 711 |
| kernel.jit.total | 732 | 711 |
| difftest.always.pass | 193 | 193 |
| difftest.always.total | 193 | 193 |
| difftest.deopt.pass | 193 | 193 |
| difftest.deopt.total | 193 | 193 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 35857 | 35857 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50745 | 50714 |
| numpytest.date | 2026-11-15 | 2026-11-08 |
| libtest.date | 2026-11-09 | 2026-11-09 |
| capi.implemented | 1152 | 1148 |
| capi.stubs | 476 | 480 |
| capi.total | 1628 | 1628 |
| numpy.pass | 50745 | - |
| numpy.xfail | 30 | - |
| numpy.skip | 1098 | - |
| numpy.error | 351 | - |
| numpy.fail | 47 | - |
| numpy.xpass | 2 | - |

## Problems

- gate check-kernel-jit failed
