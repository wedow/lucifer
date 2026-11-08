---
id: luc-ci4f
status: closed
deps: []
links: []
created: 2026-11-08T18:22:25Z
type: task
priority: 1
tags: [jit, perf]
---
# jit: cold code and compile cost (census mechanisms 1 and 2)

Census 2026-11-08 mechanisms 1 (interpreter fallback, 8.9%) and 2 (compile cost, 8.7%). Interpreted module/cold code (luc-qpu2), interpreted generator resumes (luc-s4lm, luc-j7zk, luc-9e70), the retire rule that leaves kernel units interpreted (BufferedReader.read, _lru_cache_wrapper.wrapper, chain.__next__, _asend_send), compile cost on cli programs and generated regex matchers (regex_v8, regex_compile), background compile (luc-f7cn) if justified. Primary target: the async_tree family.

## Acceptance Criteria

Interpreted resumes, retired units and compile seconds per process before/after; async_tree family, regex_v8/regex_compile, cli suite and pyperformance geomean master vs branch vs 3.14t best of 3; AWFY no regression; five gates 0.


## Notes

**2026-11-08T22:07:55Z**

Done on luc-cold. Census (27 programs, one -X lucifer_jit_stats run each, master 6990812 vs branch): async_generators interpreted instructions 321M -> 45M and retired units 49 -> 12; async_tree family 32-42M -> 20-26M, retired 2 -> 0, error exits 284K -> 6.5K (225K now leave as the direct call's result); pycodestyle 80M -> 58M, retired 61 -> 0; pyflakes retired 26 -> 16. Compile ms per process: regex_v8 900 -> 384, regex_compile 938 -> 448, markdown 299 -> 182, pygments 371 -> 220, black 214 -> 119. Timing (bench host, best of 3, vs 3.14t): async_tree family 10.22x -> 9.51x (branch/master 0.931), regex_v8+regex_compile 5.85x -> 5.50x, cli 7.14x -> 6.43x (0.900; pycodestyle 0.81), pyperformance 70 programs 5.10x -> 4.93x (0.966), AWFY 0.403 -> 0.398 (CD 0.80; DeltaBlue and Havlak equal on recheck). Five gates 0. Leftovers: luc-f7cn (data note), luc-j7zk, luc-qpu2 (note), luc-po0x, luc-ueic.
