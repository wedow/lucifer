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

