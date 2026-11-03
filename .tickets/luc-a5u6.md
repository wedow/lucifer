---
id: luc-a5u6
status: closed
deps: [luc-66sy]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
parent: luc-nsmh
tags: [milestone, slice]
---
# M0.1: 46 named difftest programs pass

Fix semantics until the 46 int/dict/closure/comprehension/augassign/sort/list programs listed in docs/DESIGN.md 9.4 match the oracle.

## Acceptance Criteria

tools/difftest run --oracle $ORACLE_T --python bin/lucifer: the 46 programs of docs/DESIGN.md 9.4 PASS; the manifest records the total PASS count.


## Notes

**2026-11-03T14:26:35Z**

Met by master after luc-66sy: all 46 named programs PASS in results/difftest/lucifer.tsv (153/164 PASS overall, 11 FAIL, 0 NOTRUN).
