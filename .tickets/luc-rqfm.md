---
id: luc-rqfm
status: closed
deps: []
links: []
created: 2026-11-17T14:49:37Z
type: task
priority: 3
---
# fuzzdiff dictset: set orders that follow hash(nan) flake between interpreters

Every dictset_diff case the 11-17 deopt floor lost (~23, a different set each run) has two NaNs in its pool. hash(nan) is the float's address (>>4), so the iteration order of a set holding them follows heap layout: the oracle gives the same order run to run (same layout), lucifer's order differs by mode and by run. The second oracle run cannot tell: tried moving its heap (allocating 3 floats+objects first) plus comparing a varying list as a multiset; losses went 23 -> 17, because many small sets keep their order under any one shift. Options: several shifted oracle runs with a part fixed only where all agree; or treat a list as an unordered set when its oracle runs are permutations across N shifts; or ask upstream (python-formalization) to give its NaNs a hash that is not their address. Not a runtime bug.


## Notes

**2026-11-19T13:56:10Z**

The pinned `difftest/dictset_diff.py` records the two pool NaNs' hashes and compares exact set iteration order and pop results. The worker now gives those two distinct NaN keys fixed hashes on both interpreters, so their placement no longer depends on the heap. The harness regression failed with address-derived hashes and passed with the fixed keys.

Comparing the original and fixed workers against CPython 3.14t on the same Lucifer binary in deopt mode, 140 of 2,000 cases changed from mismatch to match, all with NaNs in the pool. Losses against the recorded floor went from 28 to zero across seeds 1 and 2. The remaining mismatches, skips, and oracle errors occurred with both workers.
