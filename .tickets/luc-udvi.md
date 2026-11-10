---
id: luc-udvi
status: closed
deps: []
links: []
created: 2026-11-10T09:01:35Z
type: bug
priority: 1
assignee: Greg Wedow
---
# pyperformance docutils gives wrong output since the 11-10 daily

results/status-2026-11-10: corpus wrong: pyperformance.docutils. Was correct on 11-09. Bisect the 11-09..11-10 merges.


## Notes

**2026-11-10T10:55:43Z**

Not a runtime regression: lucifer's docutils output matches CPython (content hash check passes, JIT on and off). The corpus check hashes a pyperf program's stderr; the CPython reference was recorded after the calibrating run had compiled the site, but with a reference the calibrating run was skipped, so on a fresh daily site lucifer's checked run compiled docutils and printed the same SyntaxWarning CPython prints (latex2mathml.py:954, '\i'). 11-09 passed only because Monday's reference refresh ran CPython on the site first; 11-08 and 11-10 had no refresh. Fix: corpus always does the one-loop run first.
