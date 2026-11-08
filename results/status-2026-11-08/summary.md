# Lucifer status 2026-11-08

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | JIT on | on/CPython | on vs yesterday |
|---|---|---|---|---|
| Bounce | 1729.1 | 669.1 | 0.387 | -28.7% |
| CD | 2573.9 | 1223.6 | 0.475 | -82.3% |
| DeltaBlue | 636.6 | 529.8 | 0.832 | -61.5% |
| Havlak | 3519.8 | 3551.6 | 1.009 | -52.5% |
| Json | 874.4 | 1373.3 | 1.571 | -39.3% |
| List | 1055.9 | 721.5 | 0.683 | -12.6% |
| Mandelbrot | 1368.0 | 318.6 | 0.233 | -11.0% |
| NBody | 1960.5 | 407.6 | 0.208 | -15.2% |
| Permute | 2073.0 | 745.6 | 0.360 | -21.3% |
| Queens | 1003.3 | 344.0 | 0.343 | -29.0% |
| Richards | 4348.4 | 1998.1 | 0.459 | -9.0% |
| Sieve | 2153.1 | 621.2 | 0.289 | -29.1% |
| Storage | 1738.5 | 1200.4 | 0.690 | -80.8% |
| Towers | 1399.0 | 500.6 | 0.358 | -26.0% |

Geomean JIT on / CPython: 0.478 (yesterday 0.826)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3101 | 2601 | 361 | 17.94 | 7716.16 | 0.139 | 1.00 | 1.00 | 1.00 | +11.9% |
| 2 | 2867 | 4372 | 626 | 122.56 | 148.06 | 0.143 | 0.46 | 0.84 | 0.87 | +14.8% |
| 4 | 2191 | 8110 | 1296 | 52.82 | 188.61 | 0.160 | 0.18 | 0.78 | 0.90 | +17.7% |
| 8 | 2160 | 10103 | 1734 | 40.81 | 74.04 | 0.172 | 0.09 | 0.49 | 0.60 | +14.8% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday |
|---|---|---|---|---|
| awfy | 14 | 0 | 0.478 | 0.826 |
| cli | 6 | 0 | 10.091 | - |
| pyperformance | 96 | 26 | 5.876 | - |
| web | 1 | 0 | 5.828 | 6.688 |
| all | 117 | 26 | 4.139 | 0.949 |

## Gates

| target | result | detail |
|---|---|---|
| check-blocking | pass |  |
| check-globals | pass |  |
| check-capi | pass |  |
| check-kernel | pass | 611/611 checks pass (plain) |
| check-kernel-jit | pass | 611/611 checks pass (jit); 611/611 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 611 | 593 |
| kernel.plain.total | 611 | 593 |
| kernel.jit.pass | 611 | 593 |
| kernel.jit.total | 611 | 593 |
| kernel.jit-deopt.pass | 611 | 593 |
| kernel.jit-deopt.total | 611 | 593 |
| difftest.always.pass | 165 | 164 |
| difftest.always.total | 165 | 164 |
| difftest.deopt.pass | 165 | 164 |
| difftest.deopt.total | 165 | 164 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 1993 | 1993 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50714 | 50299 |
| numpytest.date | 2026-11-08 | 2026-11-05 |
| libtest-core.lucifer.pass | 1993 | 1993 |
| libtest-core.oracle.pass | 2157 | 2157 |
| libtest-core.date | 2026-11-04 | 2026-11-04 |
| capi.implemented | 1083 | 1083 |
| capi.stubs | 545 | 545 |
| capi.total | 1628 | 1628 |
| numpy.pass | 50714 | - |
| numpy.xfail | 30 | - |
| numpy.skip | 1103 | - |
| numpy.error | 349 | - |
| numpy.fail | 59 | - |
| numpy.timeout | 1 | - |
| numpy.notrun | 15 | - |
| numpy.xpass | 2 | - |

## Problems

- corpus fail: pyperformance.asyncio_tcp, pyperformance.asyncio_tcp_ssl, pyperformance.asyncio_websockets, pyperformance.concurrent_imap, pyperformance.coverage, pyperformance.chameleon, pyperformance.fastapi, pyperformance.networkx, pyperformance.networkx_connected_components, pyperformance.networkx_k_core, pyperformance.pickle, pyperformance.pickle_dict, pyperformance.pickle_list, pyperformance.sqlalchemy_declarative, pyperformance.sqlalchemy_imperative, pyperformance.sqlite_synth, pyperformance.sympy, pyperformance.tornado_http, pyperformance.typing_runtime_protocols, pyperformance.unpickle, pyperformance.unpickle_list, pyperformance.xdsl
- corpus timeout: pyperformance.scimark
- corpus wrong: pyperformance.docutils, pyperformance.sphinx, pyperformance.xml_etree
