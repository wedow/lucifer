# Lucifer status 2026-11-26

## AWFY (best iteration, ms; ratio is JIT on / CPython)

| benchmark | CPython | PyPy | JIT on | on/CPython | on/PyPy | on vs yesterday |
|---|---|---|---|---|---|---|
| Bounce | 1741.2 | 37.8 | 154.0 | 0.088 | 4.077 | -2.5% |
| CD | 2600.0 | 263.6 | 1068.3 | 0.411 | 4.053 | +0.0% |
| DeltaBlue | 630.3 | 76.3 | 569.2 | 0.903 | 7.460 | +14.4% |
| Havlak | 3471.8 | 462.0 | 2792.7 | 0.804 | 6.045 | +22.0% |
| Json | 884.8 | 198.0 | 401.6 | 0.454 | 2.028 | +4.3% |
| List | 1059.1 | 242.4 | 271.4 | 0.256 | 1.119 | -40.5% |
| Mandelbrot | 1393.8 | 50.9 | 55.0 | 0.039 | 1.080 | +4.0% |
| NBody | 1939.3 | 63.8 | 336.0 | 0.173 | 5.266 | +10.2% |
| Permute | 2081.1 | 145.0 | 924.1 | 0.444 | 6.374 | +14.2% |
| Queens | 991.4 | 82.4 | 337.0 | 0.340 | 4.092 | +11.1% |
| Richards | 4262.0 | 184.8 | 1142.2 | 0.268 | 6.182 | +2.2% |
| Sieve | 2152.5 | 119.8 | 303.4 | 0.141 | 2.533 | +12.0% |
| Storage | 1727.8 | 232.2 | 1398.8 | 0.810 | 6.025 | +10.0% |
| Towers | 1379.2 | 181.9 | 383.3 | 0.278 | 2.107 | +4.4% |

Geomean JIT on / CPython: 0.288 (yesterday 0.278); JIT on / PyPy: 3.547

## Idioms (cycles and instructions per inner iteration, steady state)

| idiom | lucifer cycles | lucifer instr | cpython cycles | cpython instr | pypy cycles | pypy instr | lucifer/pypy | lucifer/cpython |
|---|---|---|---|---|---|---|---|---|
| loads | 15 | 60 | 247 | 765 | 13 | 38 | 1.20 | 0.06 |
| stores | 42 | 173 | 148 | 322 | 8 | 26 | 4.94 | 0.28 |
| pstores | 25 | 101 | 92 | 235 | 8 | 26 | 3.36 | 0.28 |
| fstores | 23 | 89 | 203 | 577 | 10 | 30 | 2.18 | 0.11 |
| calls | 15 | 58 | 209 | 691 | 10 | 29 | 1.51 | 0.07 |
| poly | 126 | 441 | 379 | 1043 | 23 | 67 | 5.51 | 0.33 |
| alloc_local | 21 | 65 | 734 | 2037 | 5 | 22 | 3.79 | 0.03 |
| alloc_escape | 119 | 403 | 544 | 1486 | 26 | 38 | 4.64 | 0.22 |
| listsum | 11 | 42 | 157 | 472 | 5 | 19 | 2.19 | 0.07 |
| listidx | 56 | 198 | 159 | 453 | 6 | 24 | 10.11 | 0.35 |
| dictcount | 426 | 1258 | 402 | 1124 | 101 | 310 | 4.22 | 1.06 |
| dictint | 158 | 508 | 176 | 560 | 29 | 98 | 5.47 | 0.90 |
| strjoin | 378 | 1036 | 510 | 1351 | 78 | 294 | 4.86 | 0.74 |
| strfmt | 627 | 1740 | 1036 | 2611 | 162 | 502 | 3.87 | 0.60 |
| strmeth | 470 | 1393 | 584 | 1651 | 88 | 241 | 5.35 | 0.81 |
| genexp | 181 | 583 | 182 | 561 | 36 | 74 | 5.07 | 0.99 |
| listcomp | 89 | 228 | 131 | 384 | 16 | 53 | 5.59 | 0.68 |
| tupunpack | 19 | 79 | 153 | 490 | 10 | 29 | 2.01 | 0.13 |
| kwcall | 27 | 117 | 391 | 1200 | 8 | 34 | 3.37 | 0.07 |
| starcall | 30 | 121 | 1281 | 3368 | 5 | 21 | 5.51 | 0.02 |
| tryexc | 460 | 1173 | 552 | 1457 | 31 | 112 | 14.87 | 0.83 |
| isinst | 7 | 31 | 429 | 1283 | 10 | 27 | 0.78 | 0.02 |
| floatloop | 17 | 58 | 217 | 629 | 8 | 18 | 2.13 | 0.08 |
| closure | 26 | 106 | 237 | 758 | 5 | 21 | 5.01 | 0.11 |
| while_attr | 13 | 51 | 132 | 424 | 8 | 31 | 1.71 | 0.10 |
| slots_rw | 94 | 304 | 156 | 435 | 10 | 28 | 9.81 | 0.60 |
| dataclass_read | 11 | 48 | 191 | 614 | 11 | 32 | 1.06 | 0.06 |
| property_rw | 65 | 227 | 541 | 1583 | 10 | 28 | 6.81 | 0.12 |
| staticcall | 14 | 51 | 351 | 1086 | 5 | 18 | 3.00 | 0.04 |
| classcall | 15 | 51 | 437 | 1315 | 5 | 21 | 2.83 | 0.03 |
| boundvar | 23 | 98 | 232 | 756 | 5 | 21 | 4.54 | 0.10 |
| lambdacall | 18 | 71 | 203 | 671 | 8 | 21 | 2.30 | 0.09 |
| defaultcall | 26 | 111 | 232 | 779 | 5 | 21 | 5.02 | 0.11 |
| varargs | 301 | 832 | 604 | 1767 | 5 | 18 | 63.36 | 0.50 |
| supercall | 22 | 77 | 543 | 1621 | 7 | 27 | 3.25 | 0.04 |
| alloc_kwinit | 13 | 45 | 1369 | 3630 | 5 | 18 | 2.73 | 0.01 |
| alloc_dropped | 13 | 38 | 438 | 1270 | 4 | 14 | 2.90 | 0.03 |
| for_tuple | 10 | 42 | 84 | 286 | 6 | 23 | 1.73 | 0.12 |
| for_dict | 91 | 267 | 122 | 391 | 17 | 49 | 5.41 | 0.75 |
| for_items | 214 | 552 | 187 | 590 | 22 | 63 | 9.80 | 1.14 |
| for_set | 106 | 330 | 142 | 387 | 16 | 48 | 6.75 | 0.75 |
| for_str | 83 | 280 | 78 | 253 | 19 | 52 | 4.44 | 1.06 |
| for_range | 11 | 40 | 92 | 304 | 5 | 18 | 2.34 | 0.12 |
| for_enumerate | 22 | 85 | 247 | 747 | 10 | 24 | 2.34 | 0.09 |
| for_zip | 40 | 163 | 264 | 799 | 9 | 34 | 4.39 | 0.15 |
| for_reversed | 96 | 286 | 225 | 724 | 6 | 23 | 16.56 | 0.42 |
| while_index | 13 | 60 | 178 | 544 | 6 | 19 | 2.10 | 0.08 |
| list_appendpop | 316 | 866 | 288 | 786 | 24 | 72 | 12.94 | 1.10 |
| list_index | 419 | 1267 | 511 | 1438 | 32 | 90 | 12.95 | 0.82 |
| list_slice | 498 | 1216 | 436 | 1199 | 27 | 66 | 18.67 | 1.14 |
| list_sort | 2179 | 5173 | 87 | 266 | 54 | 182 | 40.04 | 24.98 |
| list_in | 378 | 1038 | 436 | 1263 | 45 | 120 | 8.45 | 0.87 |
| dict_strset | 155 | 503 | 167 | 474 | 52 | 163 | 2.99 | 0.93 |
| dict_in | 119 | 421 | 131 | 424 | 25 | 95 | 4.68 | 0.91 |
| dict_tuplekey | 162 | 486 | 201 | 567 | 80 | 222 | 2.03 | 0.81 |
| dict_update | 618 | 1568 | 933 | 2464 | 219 | 586 | 2.83 | 0.66 |
| dict_setdefault | 361 | 1078 | 335 | 1004 | 28 | 92 | 12.98 | 1.08 |
| dict_pop | 659 | 1840 | 482 | 1341 | 103 | 340 | 6.42 | 1.37 |
| set_addin | 212 | 621 | 237 | 668 | 41 | 132 | 5.19 | 0.89 |
| str_concat | 182 | 519 | 208 | 539 | 23 | 57 | 7.91 | 0.87 |
| str_format | 5132 | 13406 | 936 | 2314 | 148 | 426 | 34.74 | 5.48 |
| str_split | 1282 | 3350 | 825 | 2142 | 207 | 667 | 6.20 | 1.55 |
| str_replace | 508 | 1250 | 615 | 1584 | 202 | 581 | 2.51 | 0.83 |
| str_slice | 454 | 1326 | 444 | 1152 | 5 | 18 | 95.68 | 1.02 |
| str_in | 210 | 646 | 204 | 645 | 70 | 240 | 2.99 | 1.03 |
| str_encode | 469 | 1327 | 283 | 877 | 5 | 18 | 102.10 | 1.66 |
| bytes_index | 14 | 56 | 145 | 479 | 8 | 23 | 1.73 | 0.10 |
| int_divmod | 78 | 83 | 433 | 1336 | 68 | 65 | 1.14 | 0.18 |
| int_pow | 1141 | 2597 | 297 | 871 | 7 | 24 | 155.26 | 3.85 |
| mixed_arith | 15 | 52 | 157 | 479 | 8 | 17 | 1.81 | 0.09 |
| bigint | 3071 | 6917 | 445 | 1315 | 186 | 633 | 16.48 | 6.90 |
| compare_chain | 13 | 43 | 159 | 515 | 16 | 46 | 0.77 | 0.08 |
| tuple_build | 19 | 63 | 183 | 630 | 5 | 22 | 3.54 | 0.10 |
| genfunc | 13 | 46 | 143 | 484 | 31 | 63 | 0.42 | 0.09 |
| dictcomp | 277 | 676 | 207 | 543 | 67 | 192 | 4.14 | 1.34 |
| setcomp | 463 | 1275 | 128 | 384 | 33 | 108 | 14.12 | 3.61 |
| try_noraise | 12 | 40 | 93 | 312 | 5 | 18 | 2.61 | 0.13 |
| raise_catch | 522 | 1376 | 749 | 1969 | 7 | 21 | 79.94 | 0.70 |
| with_stmt | 582 | 1437 | 508 | 1563 | 5 | 18 | 122.41 | 1.15 |
| type_is | 7 | 31 | 173 | 563 | 10 | 27 | 0.78 | 0.04 |
| getattr_const | 85 | 326 | 286 | 922 | 10 | 29 | 8.84 | 0.30 |
| hasattr_const | 124 | 405 | 226 | 745 | 8 | 23 | 16.53 | 0.55 |
| builtins_small | 338 | 1311 | 1386 | 3574 | 350 | 772 | 0.97 | 0.24 |
| anyall | 63 | 280 | 424 | 1324 | 119 | 262 | 0.53 | 0.15 |
| global_rw | 282 | 708 | 210 | 624 | 6 | 18 | 44.20 | 1.34 |
| nonlocal_rw | 114 | 372 | 248 | 766 | 9 | 27 | 13.17 | 0.46 |

Geomean lucifer/pypy cycles over 86 idioms: 5.17
Geomean lucifer/cpython cycles over 86 idioms: 0.31

Geomean Lucifer/PyPy: 5.165 (yesterday 5.287); Lucifer/CPython: 0.314 (yesterday 0.321)

## Web (Flask Todo app, thread per connection, req/s; efficiency = req/s / (CPUs x 1-CPU req/s))

| CPUs | 3.14 | 3.14t | Lucifer | Lucifer p50 ms | Lucifer p99 ms | Lucifer/3.14t | eff 3.14 | eff 3.14t | eff Lucifer | Lucifer vs yesterday |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 3059 | 2612 | 536 | 48.28 | 8050.75 | 0.205 | 1.00 | 1.00 | 1.00 | -24.5% |
| 2 | 2756 | 3681 | 1067 | 64.97 | 95.96 | 0.290 | 0.45 | 0.70 | 1.00 | -15.7% |
| 4 | 2750 | 7571 | 1992 | 36.88 | 348.64 | 0.263 | 0.22 | 0.72 | 0.93 | -14.4% |
| 8 | 2425 | 10234 | 3376 | 20.19 | 58.23 | 0.330 | 0.10 | 0.49 | 0.79 | -5.5% |

## Suites (time over CPython 3.14t, lower is faster; the README speed is 1 / the all row)

| suite | programs | failing | geomean | yesterday | PyPy failing | PyPy geomean | Lucifer/PyPy |
|---|---|---|---|---|---|---|---|
| awfy | 14 | 0 | 0.288 | 0.278 | 0 | 0.081 | 3.547 |
| cadence | 5 | 0 | 2.250 | 2.040 | 5 | - | - |
| cli | 6 | 0 | 4.890 | 4.654 | 0 | 2.550 | 1.918 |
| pyperformance | 94 | 0 | 2.000 | 1.846 | 4 | 0.595 | 3.304 |
| pyperformance-warm | 94 | 0 | 1.843 | 1.717 | 4 | 0.490 | 3.700 |
| startup | 3 | 0 | 2.594 | 2.495 | 0 | 1.745 | 1.487 |
| web | 1 | 0 | 3.031 | 2.865 | not run | - | - |
| all | 123 | 0 | 1.700 | 1.580 | 9 | 0.516 | 3.170 |

cli, cadence and startup are end-to-end: whole-process wall time, startup, import and compile included. pyperformance-warm repeats pyperformance with more warm-up, so its gap to pyperformance is the cost of warming up; it is not in the all row. PyPy is timed with the CPython reference at CPython's loop counts; PyPy failing counts programs it cannot run or whose output differs, not run marks a suite it is not timed on, and Lucifer/PyPy is the geomean over the programs both run.

## Gates

| target | result | detail |
|---|---|---|
| check-fast-float | pass |  |
| check-blocking | pass |  |
| check-globals | FAIL |  |
| check-capi | pass |  |
| check-kernel | pass | 971/971 checks pass (plain) |
| check-kernel-jit | FAIL | FAIL qsbr_retire_finalizer; 970/971 checks pass (jit) |
| check-embed | FAIL |  |
| check-jit | pass | 0 failing; PASS difftest always; PASS difftest deopt |

## Conformance

| metric | value | previous |
|---|---|---|
| kernel.plain.pass | 971 | 919 |
| kernel.plain.total | 971 | 920 |
| kernel.jit.pass | 970 | 919 |
| kernel.jit.total | 971 | 920 |
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
| capi.implemented | 1153 | 1153 |
| capi.stubs | 475 | 475 |
| capi.total | 1628 | 1628 |
| numpy | not run | |

## Problems

- gate check-globals failed
- gate check-kernel-jit failed
- gate check-embed failed
