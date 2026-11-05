# Lucifer status 2026-11-05

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | JIT off | on/CPython | on vs yesterday |
|---|---|---|---|---|---|
| Bounce | 1729.1 | 4129.6 | 5339.3 | 2.388 | - |
| CD | 2573.9 | 31920.0 | 68724.6 | 12.401 | - |
| DeltaBlue | 636.6 | 6943.1 | 2552.1 | 10.907 | - |
| Havlak | 3519.8 | 18734.3 | 13457.7 | 5.323 | - |
| Json | 874.4 | 5906.8 | 5217.1 | 6.755 | - |
| List | 1055.9 | 4138.2 | 6095.2 | 3.919 | - |
| Mandelbrot | 1368.0 | 625.5 | 146234.5 | 0.457 | - |
| NBody | 1960.5 | 2056.8 | 170132.9 | 1.049 | - |
| Permute | 2073.0 | 2333.7 | 7227.0 | 1.126 | - |
| Queens | 1003.3 | 915.4 | 2912.4 | 0.912 | - |
| Richards | 4348.4 | 9927.2 | 17209.2 | 2.283 | - |
| Sieve | 2153.1 | 2173.6 | 5754.2 | 1.010 | - |
| Storage | 1738.5 | 19432.5 | 21899.9 | 11.178 | - |
| Towers | 1399.0 | 1937.1 | 3711.1 | 1.385 | - |

Geomean JIT on / CPython: 2.640 (yesterday -)
Geomean JIT on / JIT off: 0.357
JIT on slower than off: DeltaBlue, Havlak, Json

## Gates

| target | result | detail |
|---|---|---|
| check-blocking | pass | |
| check-globals | pass | |
| check-capi | pass | PASS watchers |
| check-kernel | pass | 550/550 checks pass (plain) |
| check-kernel-jit | FAIL | FAIL threads_signal_sort; 549/550 checks pass (jit) |
| check-embed | pass | PASS embed |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 550 | - |
| kernel.plain.total | 550 | - |
| kernel.jit.pass | 549 | - |
| kernel.jit.total | 550 | - |
| difftest.oracle.pass | 164 | - |
| difftest.total | 164 | - |
| difftest.lucifer.pass | 153 | - |
| libtest.oracle.pass | 50816 | - |
| libtest.total | 56200 | - |
| libtest.lucifer.pass | 1993 | - |
| numpytest.oracle.pass | 51140 | - |
| numpytest.total | 52273 | - |
| numpytest.lucifer.pass | 2184 | - |
| capi.implemented | 934 | - |
| capi.stubs | 694 | - |
| capi.total | 1628 | - |
| numpy | not run | |

## Problems

- gate check-kernel-jit failed
