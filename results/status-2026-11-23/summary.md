# Lucifer status 2026-11-23

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1741.2 | 37.8 | 168.6 | 0.097 | 4.465 | -48.8% |
| CD | 2600.0 | 263.6 | 1036.1 | 0.399 | 3.931 | -31.7% |
| DeltaBlue | 630.3 | 76.3 | 525.8 | 0.834 | 6.892 | -18.6% |
| Havlak | 3471.8 | 462.0 | 2400.4 | 0.691 | 5.196 | -18.9% |
| Json | 884.8 | 198.0 | 450.0 | 0.509 | 2.272 | -17.9% |
| List | 1059.1 | 242.4 | 488.4 | 0.461 | 2.015 | -20.8% |
| Mandelbrot | 1393.8 | 50.9 | 53.3 | 0.038 | 1.047 | -29.6% |
| NBody | 1939.3 | 63.8 | 376.3 | 0.194 | 5.896 | -24.8% |
| Permute | 2081.1 | 145.0 | 925.0 | 0.444 | 6.380 | -10.4% |
| Queens | 991.4 | 82.4 | 323.1 | 0.326 | 3.923 | -17.0% |
| Richards | 4262.0 | 184.8 | 1227.8 | 0.288 | 6.645 | -23.9% |
| Sieve | 2152.5 | 119.8 | 288.7 | 0.134 | 2.410 | -55.2% |
| Storage | 1727.8 | 232.2 | 1310.0 | 0.758 | 5.642 | -0.3% |
| Towers | 1379.2 | 181.9 | 400.8 | 0.291 | 2.203 | -25.0% |

Geomean JIT on / CPython: 0.300 (yesterday 0.390); JIT on / PyPy: 3.695

## Idioms (cycles and instructions per inner iteration, steady state)

| idiom | lucifer cycles | lucifer instr | cpython cycles | cpython instr | pypy cycles | pypy instr | lucifer/pypy | lucifer/cpython |
|---|---|---|---|---|---|---|---|---|
| loads | 39 | 162 | 247 | 765 | 13 | 38 | 3.14 | 0.16 |
| stores | 33 | 155 | 148 | 322 | 8 | 26 | 3.84 | 0.22 |
| pstores | 24 | 109 | 92 | 235 | 8 | 26 | 3.15 | 0.26 |
| fstores | 23 | 104 | 203 | 577 | 10 | 30 | 2.21 | 0.11 |
| calls | 21 | 104 | 209 | 691 | 10 | 29 | 2.21 | 0.10 |
| poly | 112 | 474 | 379 | 1043 | 23 | 67 | 4.86 | 0.29 |
| alloc_local | 19 | 66 | 734 | 2037 | 5 | 22 | 3.50 | 0.03 |
| alloc_escape | 115 | 429 | 544 | 1486 | 26 | 38 | 4.46 | 0.21 |
| listsum | 15 | 74 | 157 | 472 | 5 | 19 | 3.17 | 0.10 |
| listidx | 60 | 222 | 159 | 453 | 6 | 24 | 10.84 | 0.37 |
| dictcount | 426 | 1276 | 402 | 1124 | 101 | 310 | 4.22 | 1.06 |
| dictint | 148 | 504 | 176 | 560 | 29 | 98 | 5.12 | 0.84 |
| strjoin | 358 | 1080 | 510 | 1351 | 78 | 294 | 4.60 | 0.70 |
| strfmt | 553 | 1740 | 1036 | 2611 | 162 | 502 | 3.41 | 0.53 |
| strmeth | 498 | 1477 | 584 | 1651 | 88 | 241 | 5.66 | 0.85 |
| genexp | 190 | 639 | 182 | 561 | 36 | 74 | 5.32 | 1.04 |
| listcomp | 84 | 227 | 131 | 384 | 16 | 53 | 5.24 | 0.64 |
| tupunpack | 28 | 126 | 153 | 490 | 10 | 29 | 2.88 | 0.18 |
| kwcall | 27 | 118 | 391 | 1200 | 8 | 34 | 3.34 | 0.07 |
| starcall | 28 | 122 | 1281 | 3368 | 5 | 21 | 5.22 | 0.02 |
| tryexc | 428 | 1159 | 552 | 1457 | 31 | 112 | 13.81 | 0.77 |
| isinst | 128 | 486 | 429 | 1283 | 10 | 27 | 13.37 | 0.30 |
| floatloop | 17 | 59 | 217 | 629 | 8 | 18 | 2.12 | 0.08 |
| closure | 25 | 107 | 237 | 758 | 5 | 21 | 4.87 | 0.11 |
| while_attr | 23 | 73 | 132 | 424 | 8 | 31 | 2.99 | 0.17 |
| slots_rw | 87 | 305 | 156 | 435 | 10 | 28 | 9.17 | 0.56 |
| dataclass_read | 26 | 112 | 191 | 614 | 11 | 32 | 2.45 | 0.14 |
| property_rw | 1467 | 3988 | 541 | 1583 | 10 | 28 | 153.94 | 2.71 |
| staticcall | 183 | 562 | 351 | 1086 | 5 | 18 | 38.27 | 0.52 |
| classcall | 494 | 1524 | 437 | 1315 | 5 | 21 | 94.62 | 1.13 |
| boundvar | 26 | 116 | 232 | 756 | 5 | 21 | 5.05 | 0.11 |
| lambdacall | 18 | 72 | 203 | 671 | 8 | 21 | 2.38 | 0.09 |
| defaultcall | 26 | 112 | 232 | 779 | 5 | 21 | 5.05 | 0.11 |
| varargs | 306 | 884 | 604 | 1767 | 5 | 18 | 64.38 | 0.51 |
| supercall | 316 | 935 | 543 | 1621 | 7 | 27 | 47.10 | 0.58 |
| alloc_kwinit | 430 | 1253 | 1369 | 3630 | 5 | 18 | 93.58 | 0.31 |
| alloc_dropped | 12 | 39 | 438 | 1270 | 4 | 14 | 2.81 | 0.03 |
| for_tuple | 12 | 54 | 84 | 286 | 6 | 23 | 2.22 | 0.15 |
| for_dict | 86 | 268 | 122 | 391 | 17 | 49 | 5.11 | 0.70 |
| for_items | 179 | 549 | 187 | 590 | 22 | 63 | 8.20 | 0.95 |
| for_set | 110 | 358 | 142 | 387 | 16 | 48 | 7.03 | 0.78 |
| for_str | 87 | 308 | 78 | 253 | 19 | 52 | 4.61 | 1.11 |
| for_range | 11 | 41 | 92 | 304 | 5 | 18 | 2.36 | 0.12 |
| for_enumerate | 25 | 101 | 247 | 747 | 10 | 24 | 2.62 | 0.10 |
| for_zip | 40 | 163 | 264 | 799 | 9 | 34 | 4.43 | 0.15 |
| for_reversed | 92 | 311 | 225 | 724 | 6 | 23 | 15.86 | 0.41 |
| while_index | 17 | 75 | 178 | 544 | 6 | 19 | 2.64 | 0.09 |
| list_appendpop | 291 | 915 | 288 | 786 | 24 | 72 | 11.90 | 1.01 |
| list_index | 433 | 1597 | 511 | 1438 | 32 | 90 | 13.37 | 0.85 |
| list_slice | 489 | 1234 | 436 | 1199 | 27 | 66 | 18.32 | 1.12 |
| list_sort | 2356 | 5664 | 87 | 266 | 54 | 182 | 43.30 | 27.01 |
| list_in | 353 | 1039 | 436 | 1263 | 45 | 120 | 7.88 | 0.81 |
| dict_strset | 134 | 483 | 167 | 474 | 52 | 163 | 2.59 | 0.80 |
| dict_in | 127 | 422 | 131 | 424 | 25 | 95 | 5.00 | 0.97 |
| dict_tuplekey | 155 | 482 | 201 | 567 | 80 | 222 | 1.94 | 0.77 |
| dict_update | 636 | 1784 | 933 | 2464 | 219 | 586 | 2.91 | 0.68 |
| dict_setdefault | 400 | 1312 | 335 | 1004 | 28 | 92 | 14.37 | 1.20 |
| dict_pop | 677 | 2113 | 482 | 1341 | 103 | 340 | 6.60 | 1.40 |
| set_addin | 218 | 675 | 237 | 668 | 41 | 132 | 5.34 | 0.92 |
| str_concat | 173 | 518 | 208 | 539 | 23 | 57 | 7.53 | 0.83 |
| str_format | 5572 | 14110 | 936 | 2314 | 148 | 426 | 37.72 | 5.95 |
| str_split | 1227 | 3362 | 825 | 2142 | 207 | 667 | 5.93 | 1.49 |
| str_replace | 433 | 1257 | 615 | 1584 | 202 | 581 | 2.14 | 0.70 |
| str_slice | 544 | 1563 | 444 | 1152 | 5 | 18 | 114.73 | 1.23 |
| str_in | 221 | 692 | 204 | 645 | 70 | 240 | 3.15 | 1.08 |
| str_encode | 471 | 1417 | 283 | 877 | 5 | 18 | 102.50 | 1.66 |
| bytes_index | 14 | 57 | 145 | 479 | 8 | 23 | 1.71 | 0.10 |
| int_divmod | 82 | 84 | 433 | 1336 | 68 | 65 | 1.20 | 0.19 |
| int_pow | 1131 | 3510 | 297 | 871 | 7 | 24 | 153.88 | 3.82 |
| mixed_arith | 15 | 53 | 157 | 479 | 8 | 17 | 1.86 | 0.10 |
| bigint | 2713 | 7251 | 445 | 1315 | 186 | 633 | 14.56 | 6.10 |
| compare_chain | 13 | 45 | 159 | 515 | 16 | 46 | 0.78 | 0.08 |
| tuple_build | 17 | 64 | 183 | 630 | 5 | 22 | 3.19 | 0.09 |
| genfunc | 13 | 48 | 143 | 484 | 31 | 63 | 0.43 | 0.09 |
| dictcomp | 236 | 690 | 207 | 543 | 67 | 192 | 3.53 | 1.14 |
| setcomp | 528 | 1477 | 128 | 384 | 33 | 108 | 16.14 | 4.13 |
| try_noraise | 11 | 41 | 93 | 312 | 5 | 18 | 2.44 | 0.12 |
| raise_catch | 519 | 1355 | 749 | 1969 | 7 | 21 | 79.55 | 0.69 |
| with_stmt | 498 | 1466 | 508 | 1563 | 5 | 18 | 104.81 | 0.98 |
| type_is | 12 | 60 | 173 | 563 | 10 | 27 | 1.25 | 0.07 |
| getattr_const | 103 | 397 | 286 | 922 | 10 | 29 | 10.77 | 0.36 |
| hasattr_const | 128 | 480 | 226 | 745 | 8 | 23 | 17.06 | 0.57 |
| builtins_small | 384 | 1439 | 1386 | 3574 | 350 | 772 | 1.10 | 0.28 |
| anyall | 63 | 281 | 424 | 1324 | 119 | 262 | 0.53 | 0.15 |
| global_rw | 230 | 687 | 210 | 624 | 6 | 18 | 36.02 | 1.09 |
| nonlocal_rw | 108 | 373 | 248 | 766 | 9 | 27 | 12.41 | 0.43 |

Geomean lucifer/pypy cycles over 86 idioms: 6.64
Geomean lucifer/cpython cycles over 86 idioms: 0.40

Geomean Lucifer/PyPy: 6.635 (yesterday -); Lucifer/CPython: 0.403 (yesterday -)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3059 | 2612 | 649 | 39.65 | 7939.91 | 0.248 | 1.00 | 1.00 | 1.00 | +20.6% |
| 2 | 2756 | 3681 | 1107 | 65.39 | 118.77 | 0.301 | 0.45 | 0.70 | 0.85 | +20.6% |
| 4 | 2750 | 7571 | 2180 | 32.65 | 65.05 | 0.288 | 0.22 | 0.72 | 0.84 | +18.9% |
| 8 | 2425 | 10234 | 3464 | 21.11 | 55.91 | 0.338 | 0.10 | 0.49 | 0.67 | +19.1% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.300 | 0.390 | 0 | 0.081 | 3.695 |
| cadence | 5 | 0 | 2.024 | 2.093 | 5 | - | - |
| cli | 6 | 0 | 5.480 | 5.447 | 0 | 2.550 | 2.149 |
| pyperformance | 94 | 0 | 1.957 | 2.220 | 4 | 0.595 | 3.228 |
| pyperformance-warm | 94 | 0 | 1.813 | 2.099 | 4 | 0.490 | 3.654 |
| startup | 3 | 0 | 2.575 | 3.353 | 0 | 1.745 | 1.476 |
| web | 1 | 0 | 2.954 | 3.558 | not run | - | - |
| all | 123 | 0 | 1.681 | 1.925 | 9 | 0.516 | 3.146 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | FAIL |  |
| check-capi | pass |  |
| check-kernel | pass | 872/872 checks pass (plain) |
| check-kernel-jit | pass | 872/872 checks pass (jit); 872/872 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 872 | 804 |
| kernel.plain.total | 872 | 806 |
| kernel.jit.pass | 872 | 804 |
| kernel.jit.total | 872 | 806 |
| kernel.jit-deopt.pass | 872 | - |
| kernel.jit-deopt.total | 872 | - |
| difftest.always.pass | 193 | 193 |
| difftest.always.total | 193 | 193 |
| difftest.deopt.pass | 193 | 193 |
| difftest.deopt.total | 193 | 193 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 40528 | 40528 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50745 | 50745 |
| numpytest.date | 2026-11-15 | 2026-11-15 |
| libtest.date | 2026-11-21 | 2026-11-21 |
| capi.implemented | 1153 | 1152 |
| capi.stubs | 475 | 476 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

- gate check-globals failed
