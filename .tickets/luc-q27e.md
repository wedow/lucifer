---
id: luc-q27e
status: closed
deps: []
links: []
created: 2026-11-17T14:36:00Z
type: task
priority: 2
---
# deopt floor: per-mode harness deadlines for stress modes

The 11-17 deopt floor (4 jobs, nice 10, loaded host) lost 1351 libtest cases to TIMEOUT (test_tarfile 758, test_io 593) and fuzzdiff pygrammar_check-s1/-s2 batch 500-749 (whole 250-case batches: generation of 750 cases before the first output line takes ~50 s alone in deopt, over the 300 s process limit under load). Alone on master: test_io passes in 60 s; test_tarfile runs 745 cases in 172 s and hits the 180 s regrtest module timeout (deopt slowness, not a hang); pygrammar batches pass in 50-62 s. tools/floor should give stress modes longer deadlines (libtest --timeout, fuzzdiff --timeout) or the daily should run deopt with fewer concurrent units.


## Notes

**2026-11-17T14:41:49Z**

Alone on master (aaab42d): test_tarfile module wall time off 104 s, jit 151 s, deopt >181 s (killed at the 180 s regrtest module timeout after 745 cases at ~0.25 s each, no hang). Raising the libtest/fuzzdiff deadlines for every unit risks the daily's wall time where a module really hangs (the deopt floor has 3641 TIMEOUT cases), so pick per-mode deadlines from build/floor/unit-seconds.json rather than one constant.

**2026-11-17T15:46:21Z**

Landed: tools/floor keeps unit seconds per mode; a Lib/test module's and fuzzdiff process's deadline is 2x its last run in the mode (never under 180/300 s); a run over 2x keeps the old seconds. Deopt alone: test_tarfile 177 s, pygrammar_check-s1/-s2 190/184 s, next deadlines 354/380/368 s. A hung worker (test_bisect) stops at 211 s with no history, at 454 s with 211 s recorded, history kept at 211. floor quick PASS (off/jit/deopt, 429 s), check-harness PASS.
