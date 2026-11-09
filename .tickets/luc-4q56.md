---
id: luc-4q56
status: closed
deps: []
links: []
created: 2026-11-09T04:05:14Z
type: task
priority: 1
---
# Full Lib/test conformance as a tracked weekly metric

The README Lib/test row covers 18 core-type modules; the libtest total compares Lucifer against CPython's full run although Lucifer never ran the rest. Run every Lib/test module on Lucifer, record it weekly (Saturdays) in the daily status, render a whole-suite README row, and triage failures by cause into tickets.


## Notes

**2026-11-09T09:47:07Z**

First whole-suite run 2026-11-09: 538 modules, Lucifer 35,857 passing of CPython 3.14t's 50,816 (70.6%), 23 min at 6 parallel modules. tools/status --libtest runs it; the daily passes it on Saturdays. Failures ticketed by cause: luc-lmao, luc-bfwp, luc-7aot, luc-221o, luc-a7v4, luc-fd8e, luc-x2i0, luc-nvfb, luc-vl7e, luc-bxi0, luc-fnqb, luc-a59q, luc-acqf, luc-mcq7, luc-8uu4, luc-wy7i.
