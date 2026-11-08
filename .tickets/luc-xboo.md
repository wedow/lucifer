---
id: luc-xboo
status: closed
deps: []
links: []
created: 2026-11-08T03:06:11Z
type: task
priority: 1
tags: [jit, perf]
---
# jit: exit and recompile policy (roadmap item 5)

Roadmap item 5 with compile cost. Replace count cliffs (GUARD_FAILS 50 -> recompile, MAX_COMPILES 12 -> disabled, keeps-exiting -> retired forever) with adaptive policy: a failing guard goes generic once and never drives a recompile loop or a disable; recompile on hot side exits; per-site refusal state so an unfixable exit stops costing an entry and an exit; retirement backs off and decays. Fold in luc-idah, luc-ffdo, luc-j4mj, luc-ucai, luc-32pn. Proof: disabled/retired units, compile counts and seconds on the 30 census programs before/after; AWFY exits near zero; corpus geomeans; -c pass and import black vs 3.14t.


## Notes

**2026-11-08T14:00:48Z**

Done on luc-policy (head after this note). Census, 30 programs, master dfd0feb vs branch: disabled units 101 -> 0, compiles 14545 -> 12514, recompiles 4566 -> 3199, compile time 27.9 s -> 9.6 s. Corpus (bench host): pyperformance 0.941x master over 71 programs (5.55x -> 5.22x CPython), cli 0.664x (10.34x -> 6.87x). AWFY 0.956x geomean, DeltaBlue 0.68, Havlak 0.76, CD 1.034 (luc-l103). import black 1.39 -> 0.85 s (3.14t 0.14), -c pass 0.031 both (0.013). Leftovers: luc-qpu2, luc-0yfw, luc-f7cn, luc-gk3v, luc-l103.
