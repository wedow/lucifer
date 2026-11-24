# Lucifer status 2026-11-24

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1741.2 | 37.8 | 152.4 | 0.088 | 4.036 | -9.6% |
| CD | 2600.0 | 263.6 | 1031.1 | 0.397 | 3.912 | -0.5% |
| DeltaBlue | 630.3 | 76.3 | 569.0 | 0.903 | 7.458 | +8.2% |
| Havlak | 3471.8 | 462.0 | 2334.9 | 0.673 | 5.054 | -2.7% |
| Json | 884.8 | 198.0 | 471.5 | 0.533 | 2.381 | +4.8% |
| List | 1059.1 | 242.4 | 481.0 | 0.454 | 1.984 | -1.5% |
| Mandelbrot | 1393.8 | 50.9 | 54.0 | 0.039 | 1.060 | +1.2% |
| NBody | 1939.3 | 63.8 | 324.7 | 0.167 | 5.088 | -13.7% |
| Permute | 2081.1 | 145.0 | 935.4 | 0.449 | 6.452 | +1.1% |
| Queens | 991.4 | 82.4 | 317.7 | 0.320 | 3.857 | -1.7% |
| Richards | 4262.0 | 184.8 | 1117.2 | 0.262 | 6.046 | -9.0% |
| Sieve | 2152.5 | 119.8 | 269.1 | 0.125 | 2.247 | -6.8% |
| Storage | 1727.8 | 232.2 | 1306.8 | 0.756 | 5.628 | -0.2% |
| Towers | 1379.2 | 181.9 | 375.6 | 0.272 | 2.065 | -6.3% |

Geomean JIT on / CPython: 0.292 (yesterday 0.300); JIT on / PyPy: 3.592

## Idioms (cycles and instructions per inner iteration, steady state)

| idiom | lucifer cycles | lucifer instr | cpython cycles | cpython instr | pypy cycles | pypy instr | lucifer/pypy | lucifer/cpython |
|---|---|---|---|---|---|---|---|---|
| loads | 18 | 84 | 247 | 765 | 13 | 38 | 1.46 | 0.07 |
| stores | 38 | 173 | 148 | 322 | 8 | 26 | 4.46 | 0.26 |
| pstores | 24 | 101 | 92 | 235 | 8 | 26 | 3.24 | 0.27 |
| fstores | 20 | 89 | 203 | 577 | 10 | 30 | 1.89 | 0.10 |
| calls | 19 | 88 | 209 | 691 | 10 | 29 | 2.01 | 0.09 |
| poly | 113 | 465 | 379 | 1043 | 23 | 67 | 4.93 | 0.30 |
| alloc_local | 19 | 66 | 734 | 2037 | 5 | 22 | 3.50 | 0.03 |
| alloc_escape | 112 | 403 | 544 | 1486 | 26 | 38 | 4.34 | 0.21 |
| listsum | 15 | 74 | 157 | 472 | 5 | 19 | 3.18 | 0.10 |
| listidx | 51 | 198 | 159 | 453 | 6 | 24 | 9.30 | 0.32 |
| dictcount | 425 | 1277 | 402 | 1124 | 101 | 310 | 4.21 | 1.06 |
| dictint | 145 | 504 | 176 | 560 | 29 | 98 | 5.03 | 0.82 |
| strjoin | 362 | 1080 | 510 | 1351 | 78 | 294 | 4.65 | 0.71 |
| strfmt | 577 | 1746 | 1036 | 2611 | 162 | 502 | 3.56 | 0.56 |
| strmeth | 503 | 1480 | 584 | 1651 | 88 | 241 | 5.72 | 0.86 |
| genexp | 192 | 641 | 182 | 561 | 36 | 74 | 5.39 | 1.06 |
| listcomp | 84 | 227 | 131 | 384 | 16 | 53 | 5.26 | 0.64 |
| tupunpack | 25 | 102 | 153 | 490 | 10 | 29 | 2.60 | 0.16 |
| kwcall | 27 | 117 | 391 | 1200 | 8 | 34 | 3.30 | 0.07 |
| starcall | 28 | 121 | 1281 | 3368 | 5 | 21 | 5.15 | 0.02 |
| tryexc | 441 | 1158 | 552 | 1457 | 31 | 112 | 14.23 | 0.80 |
| isinst | 12 | 60 | 429 | 1283 | 10 | 27 | 1.25 | 0.03 |
| floatloop | 17 | 59 | 217 | 629 | 8 | 18 | 2.07 | 0.08 |
| closure | 24 | 107 | 237 | 758 | 5 | 21 | 4.68 | 0.10 |
| while_attr | 13 | 51 | 132 | 424 | 8 | 31 | 1.71 | 0.10 |
| slots_rw | 77 | 304 | 156 | 435 | 10 | 28 | 8.08 | 0.49 |
| dataclass_read | 16 | 75 | 191 | 614 | 11 | 32 | 1.56 | 0.09 |
| property_rw | 1454 | 4010 | 541 | 1583 | 10 | 28 | 152.62 | 2.69 |
| staticcall | 182 | 561 | 351 | 1086 | 5 | 18 | 38.08 | 0.52 |
| classcall | 514 | 1522 | 437 | 1315 | 5 | 21 | 98.50 | 1.18 |
| boundvar | 23 | 99 | 232 | 756 | 5 | 21 | 4.53 | 0.10 |
| lambdacall | 18 | 71 | 203 | 671 | 8 | 21 | 2.34 | 0.09 |
| defaultcall | 26 | 111 | 232 | 779 | 5 | 21 | 5.07 | 0.11 |
| varargs | 298 | 883 | 604 | 1767 | 5 | 18 | 62.70 | 0.49 |
| supercall | 313 | 933 | 543 | 1621 | 7 | 27 | 46.62 | 0.58 |
| alloc_kwinit | 12 | 45 | 1369 | 3630 | 5 | 18 | 2.60 | 0.01 |
| alloc_dropped | 12 | 38 | 438 | 1270 | 4 | 14 | 2.61 | 0.03 |
| for_tuple | 12 | 54 | 84 | 286 | 6 | 23 | 2.23 | 0.15 |
| for_dict | 85 | 268 | 122 | 391 | 17 | 49 | 5.04 | 0.70 |
| for_items | 179 | 552 | 187 | 590 | 22 | 63 | 8.20 | 0.96 |
| for_set | 111 | 358 | 142 | 387 | 16 | 48 | 7.09 | 0.79 |
| for_str | 88 | 308 | 78 | 253 | 19 | 52 | 4.67 | 1.12 |
| for_range | 11 | 40 | 92 | 304 | 5 | 18 | 2.29 | 0.12 |
| for_enumerate | 25 | 101 | 247 | 747 | 10 | 24 | 2.60 | 0.10 |
| for_zip | 40 | 163 | 264 | 799 | 9 | 34 | 4.38 | 0.15 |
| for_reversed | 88 | 310 | 225 | 724 | 6 | 23 | 15.29 | 0.39 |
| while_index | 13 | 61 | 178 | 544 | 6 | 19 | 2.06 | 0.07 |
| list_appendpop | 292 | 913 | 288 | 786 | 24 | 72 | 11.96 | 1.01 |
| list_index | 472 | 1597 | 511 | 1438 | 32 | 90 | 14.58 | 0.92 |
| list_slice | 473 | 1233 | 436 | 1199 | 27 | 66 | 17.72 | 1.09 |
| list_sort | 2547 | 6008 | 87 | 266 | 54 | 182 | 46.80 | 29.20 |
| list_in | 345 | 1038 | 436 | 1263 | 45 | 120 | 7.71 | 0.79 |
| dict_strset | 141 | 483 | 167 | 474 | 52 | 163 | 2.73 | 0.85 |
| dict_in | 127 | 421 | 131 | 424 | 25 | 95 | 4.97 | 0.96 |
| dict_tuplekey | 159 | 481 | 201 | 567 | 80 | 222 | 1.99 | 0.79 |
| dict_update | 629 | 1783 | 933 | 2464 | 219 | 586 | 2.88 | 0.67 |
| dict_setdefault | 417 | 1310 | 335 | 1004 | 28 | 92 | 14.99 | 1.25 |
| dict_pop | 691 | 2111 | 482 | 1341 | 103 | 340 | 6.74 | 1.43 |
| set_addin | 218 | 675 | 237 | 668 | 41 | 132 | 5.34 | 0.92 |
| str_concat | 174 | 518 | 208 | 539 | 23 | 57 | 7.56 | 0.83 |
| str_format | 5538 | 14102 | 936 | 2314 | 148 | 426 | 37.49 | 5.92 |
| str_split | 1225 | 3361 | 825 | 2142 | 207 | 667 | 5.93 | 1.49 |
| str_replace | 438 | 1258 | 615 | 1584 | 202 | 581 | 2.16 | 0.71 |
| str_slice | 546 | 1566 | 444 | 1152 | 5 | 18 | 115.10 | 1.23 |
| str_in | 212 | 691 | 204 | 645 | 70 | 240 | 3.02 | 1.04 |
| str_encode | 482 | 1424 | 283 | 877 | 5 | 18 | 104.99 | 1.70 |
| bytes_index | 14 | 57 | 145 | 479 | 8 | 23 | 1.67 | 0.10 |
| int_divmod | 82 | 83 | 433 | 1336 | 68 | 65 | 1.20 | 0.19 |
| int_pow | 1141 | 3510 | 297 | 871 | 7 | 24 | 155.22 | 3.85 |
| mixed_arith | 15 | 53 | 157 | 479 | 8 | 17 | 1.82 | 0.09 |
| bigint | 2708 | 7262 | 445 | 1315 | 186 | 633 | 14.53 | 6.09 |
| compare_chain | 12 | 44 | 159 | 515 | 16 | 46 | 0.76 | 0.08 |
| tuple_build | 17 | 64 | 183 | 630 | 5 | 22 | 3.16 | 0.09 |
| genfunc | 12 | 47 | 143 | 484 | 31 | 63 | 0.40 | 0.09 |
| dictcomp | 238 | 690 | 207 | 543 | 67 | 192 | 3.56 | 1.15 |
| setcomp | 513 | 1481 | 128 | 384 | 33 | 108 | 15.67 | 4.01 |
| try_noraise | 11 | 40 | 93 | 312 | 5 | 18 | 2.37 | 0.12 |
| raise_catch | 528 | 1354 | 749 | 1969 | 7 | 21 | 80.91 | 0.70 |
| with_stmt | 482 | 1464 | 508 | 1563 | 5 | 18 | 101.49 | 0.95 |
| type_is | 12 | 60 | 173 | 563 | 10 | 27 | 1.24 | 0.07 |
| getattr_const | 106 | 400 | 286 | 922 | 10 | 29 | 11.02 | 0.37 |
| hasattr_const | 131 | 480 | 226 | 745 | 8 | 23 | 17.37 | 0.58 |
| builtins_small | 390 | 1437 | 1386 | 3574 | 350 | 772 | 1.11 | 0.28 |
| anyall | 63 | 280 | 424 | 1324 | 119 | 262 | 0.53 | 0.15 |
| global_rw | 222 | 686 | 210 | 624 | 6 | 18 | 34.79 | 1.05 |
| nonlocal_rw | 110 | 373 | 248 | 766 | 9 | 27 | 12.67 | 0.44 |

Geomean lucifer/pypy cycles over 86 idioms: 6.00
Geomean lucifer/cpython cycles over 86 idioms: 0.36

Geomean Lucifer/PyPy: 6.000 (yesterday 6.635); Lucifer/CPython: 0.364 (yesterday 0.403)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3059 | 2612 | 549 | 45.96 | 7792.82 | 0.210 | 1.00 | 1.00 | 1.00 | -15.5% |
| 2 | 2756 | 3681 | 1018 | 58.69 | 127.40 | 0.277 | 0.45 | 0.70 | 0.93 | -8.0% |
| 4 | 2750 | 7571 | 2035 | 31.68 | 114.96 | 0.269 | 0.22 | 0.72 | 0.93 | -6.6% |
| 8 | 2425 | 10234 | 3199 | 21.95 | 96.89 | 0.313 | 0.10 | 0.49 | 0.73 | -7.7% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.292 | 0.300 | 0 | 0.081 | 3.592 |
| cadence | 5 | 0 | 2.087 | 2.024 | 5 | - | - |
| cli | 6 | 0 | 5.844 | 5.480 | 0 | 2.550 | 2.292 |
| pyperformance | 94 | 0 | 2.001 | 1.957 | 4 | 0.595 | 3.298 |
| pyperformance-warm | 94 | 0 | 1.815 | 1.813 | 4 | 0.490 | 3.654 |
| startup | 3 | 0 | 2.790 | 2.575 | 0 | 1.745 | 1.599 |
| web | 1 | 0 | 3.199 | 2.954 | not run | - | - |
| all | 123 | 0 | 1.717 | 1.681 | 9 | 0.516 | 3.207 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | FAIL |  |
| check-capi | pass |  |
| check-kernel | pass | 888/888 checks pass (plain) |
| check-kernel-jit | pass | 888/888 checks pass (jit); 888/888 checks pass (jit-deopt) |
| check-embed | pass |  |
| check-jit | FAIL | FAIL classes: passes, and is still listed in NOT_BUILT; FAIL borrowed: 242999 at 20000 iterations, 390999 at 40000; 2 failing |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 888 | 872 |
| kernel.plain.total | 888 | 872 |
| kernel.jit.pass | 888 | 872 |
| kernel.jit.total | 888 | 872 |
| kernel.jit-deopt.pass | 888 | 872 |
| kernel.jit-deopt.total | 888 | 872 |
| libtest.oracle.pass | 50816 | 50816 |
| libtest.total | 56200 | 56200 |
| libtest.lucifer.pass | 40528 | 40528 |
| numpytest.oracle.pass | 51140 | 51140 |
| numpytest.total | 52273 | 52273 |
| numpytest.lucifer.pass | 50745 | 50745 |
| numpytest.date | 2026-11-15 | 2026-11-15 |
| libtest.date | 2026-11-21 | 2026-11-21 |
| capi.implemented | 1153 | 1153 |
| capi.stubs | 475 | 475 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

- gate check-globals failed
- gate check-jit failed
