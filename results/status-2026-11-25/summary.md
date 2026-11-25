# Lucifer status 2026-11-25

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1741.2 | 37.8 | 158.0 | 0.091 | 4.183 | +3.6% |
| CD | 2600.0 | 263.6 | 1067.9 | 0.411 | 4.052 | +3.6% |
| DeltaBlue | 630.3 | 76.3 | 497.5 | 0.789 | 6.520 | -12.6% |
| Havlak | 3471.8 | 462.0 | 2288.4 | 0.659 | 4.953 | -2.0% |
| Json | 884.8 | 198.0 | 385.0 | 0.435 | 1.944 | -18.3% |
| List | 1059.1 | 242.4 | 456.2 | 0.431 | 1.882 | -5.2% |
| Mandelbrot | 1393.8 | 50.9 | 52.9 | 0.038 | 1.039 | -2.0% |
| NBody | 1939.3 | 63.8 | 304.9 | 0.157 | 4.778 | -6.1% |
| Permute | 2081.1 | 145.0 | 808.9 | 0.389 | 5.580 | -13.5% |
| Queens | 991.4 | 82.4 | 303.2 | 0.306 | 3.682 | -4.5% |
| Richards | 4262.0 | 184.8 | 1117.8 | 0.262 | 6.050 | +0.1% |
| Sieve | 2152.5 | 119.8 | 270.8 | 0.126 | 2.261 | +0.6% |
| Storage | 1727.8 | 232.2 | 1271.8 | 0.736 | 5.478 | -2.7% |
| Towers | 1379.2 | 181.9 | 367.1 | 0.266 | 2.018 | -2.3% |

Geomean JIT on / CPython: 0.278 (yesterday 0.292); JIT on / PyPy: 3.427

## Idioms (cycles and instructions per inner iteration, steady state)

| idiom | lucifer cycles | lucifer instr | cpython cycles | cpython instr | pypy cycles | pypy instr | lucifer/pypy | lucifer/cpython |
|---|---|---|---|---|---|---|---|---|
| loads | 15 | 60 | 247 | 765 | 13 | 38 | 1.18 | 0.06 |
| stores | 38 | 173 | 148 | 322 | 8 | 26 | 4.47 | 0.26 |
| pstores | 25 | 101 | 92 | 235 | 8 | 26 | 3.29 | 0.27 |
| fstores | 20 | 89 | 203 | 577 | 10 | 30 | 1.91 | 0.10 |
| calls | 14 | 58 | 209 | 691 | 10 | 29 | 1.43 | 0.07 |
| poly | 117 | 470 | 379 | 1043 | 23 | 67 | 5.08 | 0.31 |
| alloc_local | 19 | 65 | 734 | 2037 | 5 | 22 | 3.46 | 0.03 |
| alloc_escape | 115 | 403 | 544 | 1486 | 26 | 38 | 4.45 | 0.21 |
| listsum | 10 | 42 | 157 | 472 | 5 | 19 | 2.16 | 0.07 |
| listidx | 50 | 197 | 159 | 453 | 6 | 24 | 9.06 | 0.31 |
| dictcount | 429 | 1236 | 402 | 1124 | 101 | 310 | 4.26 | 1.07 |
| dictint | 151 | 505 | 176 | 560 | 29 | 98 | 5.23 | 0.86 |
| strjoin | 354 | 1035 | 510 | 1351 | 78 | 294 | 4.55 | 0.69 |
| strfmt | 586 | 1745 | 1036 | 2611 | 162 | 502 | 3.62 | 0.57 |
| strmeth | 469 | 1394 | 584 | 1651 | 88 | 241 | 5.34 | 0.80 |
| genexp | 172 | 580 | 182 | 561 | 36 | 74 | 4.82 | 0.94 |
| listcomp | 82 | 228 | 131 | 384 | 16 | 53 | 5.12 | 0.63 |
| tupunpack | 18 | 79 | 153 | 490 | 10 | 29 | 1.93 | 0.12 |
| kwcall | 27 | 117 | 391 | 1200 | 8 | 34 | 3.31 | 0.07 |
| starcall | 28 | 121 | 1281 | 3368 | 5 | 21 | 5.11 | 0.02 |
| tryexc | 442 | 1162 | 552 | 1457 | 31 | 112 | 14.27 | 0.80 |
| isinst | 7 | 31 | 429 | 1283 | 10 | 27 | 0.77 | 0.02 |
| floatloop | 16 | 58 | 217 | 629 | 8 | 18 | 2.04 | 0.08 |
| closure | 24 | 106 | 237 | 758 | 5 | 21 | 4.67 | 0.10 |
| while_attr | 13 | 51 | 132 | 424 | 8 | 31 | 1.70 | 0.10 |
| slots_rw | 78 | 304 | 156 | 435 | 10 | 28 | 8.16 | 0.50 |
| dataclass_read | 11 | 48 | 191 | 614 | 11 | 32 | 1.08 | 0.06 |
| property_rw | 59 | 223 | 541 | 1583 | 10 | 28 | 6.14 | 0.11 |
| staticcall | 186 | 567 | 351 | 1086 | 5 | 18 | 38.88 | 0.53 |
| classcall | 515 | 1538 | 437 | 1315 | 5 | 21 | 98.71 | 1.18 |
| boundvar | 22 | 98 | 232 | 756 | 5 | 21 | 4.39 | 0.10 |
| lambdacall | 18 | 71 | 203 | 671 | 8 | 21 | 2.31 | 0.09 |
| defaultcall | 25 | 111 | 232 | 779 | 5 | 21 | 5.01 | 0.11 |
| varargs | 294 | 828 | 604 | 1767 | 5 | 18 | 61.98 | 0.49 |
| supercall | 20 | 77 | 543 | 1621 | 7 | 27 | 2.92 | 0.04 |
| alloc_kwinit | 12 | 45 | 1369 | 3630 | 5 | 18 | 2.54 | 0.01 |
| alloc_dropped | 11 | 38 | 438 | 1270 | 4 | 14 | 2.57 | 0.03 |
| for_tuple | 9 | 42 | 84 | 286 | 6 | 23 | 1.69 | 0.11 |
| for_dict | 89 | 273 | 122 | 391 | 17 | 49 | 5.29 | 0.73 |
| for_items | 183 | 556 | 187 | 590 | 22 | 63 | 8.38 | 0.98 |
| for_set | 106 | 332 | 142 | 387 | 16 | 48 | 6.74 | 0.75 |
| for_str | 79 | 280 | 78 | 253 | 19 | 52 | 4.21 | 1.01 |
| for_range | 11 | 40 | 92 | 304 | 5 | 18 | 2.22 | 0.12 |
| for_enumerate | 22 | 85 | 247 | 747 | 10 | 24 | 2.25 | 0.09 |
| for_zip | 40 | 163 | 264 | 799 | 9 | 34 | 4.42 | 0.15 |
| for_reversed | 83 | 286 | 225 | 724 | 6 | 23 | 14.32 | 0.37 |
| while_index | 13 | 60 | 178 | 544 | 6 | 19 | 2.05 | 0.07 |
| list_appendpop | 286 | 866 | 288 | 786 | 24 | 72 | 11.72 | 0.99 |
| list_index | 385 | 1267 | 511 | 1438 | 32 | 90 | 11.90 | 0.75 |
| list_slice | 487 | 1236 | 436 | 1199 | 27 | 66 | 18.24 | 1.12 |
| list_sort | 2195 | 5235 | 87 | 266 | 54 | 182 | 40.33 | 25.16 |
| list_in | 361 | 1038 | 436 | 1263 | 45 | 120 | 8.07 | 0.83 |
| dict_strset | 145 | 483 | 167 | 474 | 52 | 163 | 2.79 | 0.87 |
| dict_in | 126 | 421 | 131 | 424 | 25 | 95 | 4.92 | 0.96 |
| dict_tuplekey | 162 | 486 | 201 | 567 | 80 | 222 | 2.03 | 0.81 |
| dict_update | 562 | 1564 | 933 | 2464 | 219 | 586 | 2.57 | 0.60 |
| dict_setdefault | 336 | 1078 | 335 | 1004 | 28 | 92 | 12.06 | 1.00 |
| dict_pop | 593 | 1840 | 482 | 1341 | 103 | 340 | 5.78 | 1.23 |
| set_addin | 199 | 620 | 237 | 668 | 41 | 132 | 4.88 | 0.84 |
| str_concat | 174 | 518 | 208 | 539 | 23 | 57 | 7.59 | 0.84 |
| str_format | 5299 | 13279 | 936 | 2314 | 148 | 426 | 35.87 | 5.66 |
| str_split | 1200 | 3347 | 825 | 2142 | 207 | 667 | 5.80 | 1.45 |
| str_replace | 442 | 1249 | 615 | 1584 | 202 | 581 | 2.18 | 0.72 |
| str_slice | 747 | 2074 | 444 | 1152 | 5 | 18 | 157.53 | 1.68 |
| str_in | 210 | 646 | 204 | 645 | 70 | 240 | 2.99 | 1.03 |
| str_encode | 431 | 1326 | 283 | 877 | 5 | 18 | 93.83 | 1.52 |
| bytes_index | 14 | 56 | 145 | 479 | 8 | 23 | 1.66 | 0.10 |
| int_divmod | 82 | 83 | 433 | 1336 | 68 | 65 | 1.20 | 0.19 |
| int_pow | 1086 | 2596 | 297 | 871 | 7 | 24 | 147.72 | 3.66 |
| mixed_arith | 15 | 52 | 157 | 479 | 8 | 17 | 1.81 | 0.09 |
| bigint | 2686 | 6975 | 445 | 1315 | 186 | 633 | 14.41 | 6.04 |
| compare_chain | 13 | 43 | 159 | 515 | 16 | 46 | 0.77 | 0.08 |
| tuple_build | 16 | 63 | 183 | 630 | 5 | 22 | 3.09 | 0.09 |
| genfunc | 13 | 46 | 143 | 484 | 31 | 63 | 0.40 | 0.09 |
| dictcomp | 228 | 676 | 207 | 543 | 67 | 192 | 3.41 | 1.10 |
| setcomp | 445 | 1295 | 128 | 384 | 33 | 108 | 13.59 | 3.48 |
| try_noraise | 11 | 40 | 93 | 312 | 5 | 18 | 2.29 | 0.11 |
| raise_catch | 518 | 1354 | 749 | 1969 | 7 | 21 | 79.37 | 0.69 |
| with_stmt | 485 | 1439 | 508 | 1563 | 5 | 18 | 102.04 | 0.95 |
| type_is | 7 | 31 | 173 | 563 | 10 | 27 | 0.77 | 0.04 |
| getattr_const | 83 | 326 | 286 | 922 | 10 | 29 | 8.64 | 0.29 |
| hasattr_const | 116 | 405 | 226 | 745 | 8 | 23 | 15.36 | 0.51 |
| builtins_small | 328 | 1311 | 1386 | 3574 | 350 | 772 | 0.94 | 0.24 |
| anyall | 62 | 280 | 424 | 1324 | 119 | 262 | 0.52 | 0.15 |
| global_rw | 229 | 684 | 210 | 624 | 6 | 18 | 35.98 | 1.09 |
| nonlocal_rw | 110 | 372 | 248 | 766 | 9 | 27 | 12.66 | 0.44 |

Geomean lucifer/pypy cycles over 86 idioms: 5.29
Geomean lucifer/cpython cycles over 86 idioms: 0.32

Geomean Lucifer/PyPy: 5.287 (yesterday 6.000); Lucifer/CPython: 0.321 (yesterday 0.364)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3059 | 2612 | 710 | 39.08 | 8034.89 | 0.272 | 1.00 | 1.00 | 1.00 | +29.4% |
| 2 | 2756 | 3681 | 1265 | 55.98 | 83.60 | 0.344 | 0.45 | 0.70 | 0.89 | +24.2% |
| 4 | 2750 | 7571 | 2326 | 32.01 | 84.53 | 0.307 | 0.22 | 0.72 | 0.82 | +14.3% |
| 8 | 2425 | 10234 | 3572 | 20.61 | 86.13 | 0.349 | 0.10 | 0.49 | 0.63 | +11.7% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.278 | 0.292 | 0 | 0.081 | 3.427 |
| cadence | 5 | 0 | 2.040 | 2.087 | 5 | - | - |
| cli | 6 | 0 | 4.654 | 5.844 | 0 | 2.550 | 1.825 |
| pyperformance | 94 | 0 | 1.846 | 2.001 | 4 | 0.595 | 3.052 |
| pyperformance-warm | 94 | 0 | 1.717 | 1.815 | 4 | 0.490 | 3.451 |
| startup | 3 | 0 | 2.495 | 2.790 | 0 | 1.745 | 1.430 |
| web | 1 | 0 | 2.865 | 3.199 | not run | - | - |
| all | 123 | 0 | 1.580 | 1.717 | 9 | 0.516 | 2.953 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | FAIL |  |
| check-capi | pass |  |
| check-kernel | FAIL | FAIL jit_int_range_edges; 919/920 checks pass (plain) |
| check-kernel-jit | FAIL | FAIL jit_shared_code_collect; 919/920 checks pass (jit) |
| check-embed | pass |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 919 | 888 |
| kernel.plain.total | 920 | 888 |
| kernel.jit.pass | 919 | 888 |
| kernel.jit.total | 920 | 888 |
| difftest.always.pass | 193 | - |
| difftest.always.total | 193 | - |
| difftest.deopt.pass | 193 | - |
| difftest.deopt.total | 193 | - |
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
- gate check-kernel failed
- gate check-kernel-jit failed
