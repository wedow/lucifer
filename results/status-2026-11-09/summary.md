# Lucifer status 2026-11-09

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1737.6 | 557.3 | 0.321 | -16.7% |
| CD | 2588.9 | 992.0 | 0.383 | -18.9% |
| DeltaBlue | 642.3 | 457.8 | 0.713 | -13.6% |
| Havlak | 3485.0 | 2421.3 | 0.695 | -31.8% |
| Json | 906.7 | 1020.7 | 1.126 | -25.7% |
| List | 1085.3 | 644.1 | 0.593 | -10.7% |
| Mandelbrot | 1374.9 | 91.3 | 0.066 | -71.3% |
| NBody | 1948.3 | 334.6 | 0.172 | -17.9% |
| Permute | 2108.4 | 723.3 | 0.343 | -3.0% |
| Queens | 1002.2 | 324.2 | 0.323 | -5.8% |
| Richards | 4304.2 | 1968.3 | 0.457 | -1.5% |
| Sieve | 2169.2 | 610.4 | 0.281 | -1.7% |
| Storage | 1760.8 | 1161.9 | 0.660 | -3.2% |
| Towers | 1413.7 | 516.4 | 0.365 | +3.1% |

Geomean JIT on / CPython: 0.385 (yesterday 0.478)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3099 | 2579 | 459 | 52.25 | 7773.14 | 0.178 | 1.00 | 1.00 | 1.00 | +27.2% |
| 2 | 2870 | 4333 | 763 | 100.07 | 130.55 | 0.176 | 0.46 | 0.84 | 0.83 | +22.0% |
| 4 | 2593 | 8336 | 1508 | 49.37 | 62.01 | 0.181 | 0.21 | 0.81 | 0.82 | +16.4% |
| 8 | 2139 | 10658 | 2082 | 36.62 | 78.69 | 0.195 | 0.09 | 0.52 | 0.57 | +20.1% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.385 | 0.478 |
| cli | 6 | 0 | 7.055 | 10.091 |
| pyperformance | 96 | 0 | 4.187 | 5.876 |
| web | 1 | 0 | 5.120 | 5.828 |
| all | 117 | 0 | 3.238 | 4.139 |

## Gates

| target | result | detail |
|---|---|---|
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 651/651 checks pass (plain) |
| check-kernel-jit | pass | 651/651 checks pass (jit); 651/651 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 651 | 611 |
| kernel.plain.total | 651 | 611 |
| kernel.jit.pass | 651 | 611 |
| kernel.jit.total | 651 | 611 |
| kernel.jit-deopt.pass | 651 | 611 |
| kernel.jit-deopt.total | 651 | 611 |
| difftest.always.pass | 165 | 165 |
| difftest.always.total | 165 | 165 |
| difftest.deopt.pass | 165 | 165 |
| difftest.deopt.total | 165 | 165 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 1993 | 1993 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50714 | 50714 |
| numpytest.date | 2026-11-08 | 2026-11-08 |
| libtest-core.lucifer.pass | 1993 | 1993 |
| libtest-core.oracle.pass | 2157 | 2157 |
| libtest-core.date | 2026-11-04 | 2026-11-04 |
| capi.implemented | 1134 | 1083 |
| capi.stubs | 494 | 545 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

none
