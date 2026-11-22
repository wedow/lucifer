---
id: luc-jy33
status: closed
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# bench: idiom table (cycles and instructions per iteration against PyPy and CPython)

tools/bench/idioms: a set of small loops, one ordinary idiom each (the 25 of the parent's table to start, growing toward the language and the built-in types: attribute and item access, calls of every shape, allocation, iteration protocols, str/bytes/dict/set/list/tuple methods, exceptions, closures, generators, comprehensions, with, unpacking, arithmetic towers, class creation, descriptors, async). Each runs at two loop counts under perf stat; the row is (cycles, instructions) per inner iteration from the difference, for each runtime given. One JSON object out; a table printer; a compare of two runs. The daily status run adds the table and the geomean Lucifer/PyPy over rows to summary.md.

An idiom is a mechanism, not a program: no row may copy a benchmark's code.

Acceptance: `tools/bench/idioms time` reproduces the parent's table within 10% on a quiet core; summary.md carries it.


## 2026-11-22 tool and daily section

tools/bench/idioms (time/table/compare) over tools/bench/idiom_cases.py, 86 self-checking idioms (the parent's 25 plus 61). tools/status times the whole table as one prefixed step (Lucifer only, at the reference's loop counts and outputs; --refresh-reference times CPython and PyPy into results/status-reference.json) and adds the table and the Lucifer/PyPy and Lucifer/CPython geomeans to summary.md and summary.csv (idioms.geomean_on_over_pypy, idioms.geomean_on_over_cpython). The full table first comes from the daily.

Nine-row check, one pinned core, cycles per inner iteration (instructions), Lucifer / PyPy / CPython:

| idiom | Lucifer | PyPy | CPython | parent's table |
|---|---|---|---|---|
| loads | 55 (168) | 12 (38) | 261 (765) | 61/10/280 |
| stores | 116 (167) | 9 (26) | 153 (322) | 117/7/153 |
| calls | 39 (105) | 12 (29) | 234 (691) | 48/19/230 |
| listsum | 25 (74) | 5 (19) | 171 (472) | 26/1/175 |
| alloc_escape | 190 (498) | 34 (38) | 588 (1486) | 190/23/620 |
| kwcall | 561 (1366) | 8 (35) | 414 (1199) | 530/12/431 |
| tryexc | 1391 (3163) | 33 (112) | 554 (1457) | 1509/37/593 |
| dictcount | 462 (1274) | 112 (310) | 429 (1124) | 530/116/485 |
| strjoin | 469 (1123) | 120 (294) | 542 (1352) | 411/118/541 |

Lucifer and CPython rows are within about 10% except calls (-19%), dictcount (-13%) and strjoin (+14%); the original two-count script in the same lease gave calls 35 and strjoin 569, so those rows move between runs by that much. PyPy rows under 40 cycles differ by a few cycles absolute; use instructions there.

**2026-11-22 (per-runtime counts)**

Each runtime calibrates its own loop counts (its longer run about 0.3 s), since a count sized for the slowest runtime left PyPy's loop at a few milliseconds beside start-up and gave unstable, once negative, rows. Outputs are compared at one small fixed count instead. The reference times CPython and PyPy in one prefixed call each; the daily times Lucifer in one. Pinned recheck, PyPy cycles (instructions): keyword call 9 (34), list sum 5 (19), method call 10 (29), store 2 attributes 10 (26). The first full table comes from the daily.
