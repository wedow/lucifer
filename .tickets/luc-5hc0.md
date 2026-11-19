---
id: luc-5hc0
status: open
deps: []
links: []
created: 2026-11-19T21:51:34Z
type: bug
priority: 1
assignee: Greg Wedow
---
# json_dumps runs 14.9% more instructions on master than at 720b666

luc-36to's steady-state measurement (96 warm-up loops, 36 counted, one core, fixed hash seed, pyperformance 1.14.0's four json_dumps inputs) found ce303a3c at 509.4 M instructions per loop against 443.4 M at 720b666 (+14.89%). Code commits in range: 626805bf through e8b5a558 (region, opdisp, elem, libmods lanes and three race fixes, including the specialize_seq seqlock 74916baa). Bisect on core leases with the same method, name the mechanism that costs, and fix it in that mechanism. Check whether other programs with the same shape (attribute- and global-heavy pure-Python encoders: pprint, pickle_pure_python, logging) move the same way.


## Notes

**2026-11-19T23:03:33Z**

Reproduced with the four pyperformance 1.14.0 inputs and a fixed hash seed. Each arm ran in three fresh processes on one core, rotating the order. Counting started after 96 workload loops and stopped after 36 more.

| Revision | Median M instructions per loop |
| --- | ---: |
| `720b666` | 443.429 |
| `626805bf` | 431.830 |
| `5af3b77d` | 428.857 |
| `4f55f40f` | 427.745 |
| `b804b96a` | 510.403 |
| `22de3e96` | 508.804 |
| `ec1c1238` | 508.898 |
| `38870d81` | 411.012 |

The immediate-parent pair isolates a 19.32% rise at `b804b96a`. The first direct-entry commit was faster than the historical baseline. The element midpoint and master both showed runs near 509 M and 521 M, so the rise was already present before the race fixes.

Direct-entry budget growth inlined callees into units that made their receivers. At a method load, the evaluator bypassed bytecode feedback for a virtual receiver even when it knew the exact builtin type. Generic attribute lookup and C method binding then repeated inside the inlined path. Instruction samples over 36 loops put `type_lookup` at 558 M on `626805bf` and 1,522 M on `ec1c1238`, with increases in `lu_getattr_native` and `PyCMethod_New` as well.

Use the existing guarded no-dict method specialization for these receivers. The paired fix comparison was 508.958 M to 411.012 M instructions per loop (-19.24%), 7.31% below `720b666`.

The literal-method regression check matched the CPython output in both builds. Doubling the test from 20,000 to 40,000 iterations added 60,000 helper calls before the fix and 40,000 after it. The same program matched the oracle in the plain, JIT, and deopt kernel lanes.

The other shape probes improved from `720b666` to `ec1c1238`. Logging went from 2.087 M to 1.918 M instructions per loop, pure-Python pickle from 342.986 M to 110.013 M, and pprint from 74.532 G to 55.542 G.
