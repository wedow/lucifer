---
id: luc-jz4c
status: open
deps: []
links: []
created: 2026-11-16T12:28:00Z
type: task
priority: 2
assignee: Greg Wedow
---
# record a real JIT-off floor for ft-packages and pypi

results/floor/off/ft-packages.tsv is 163,604 ERROR (recorded before _lsprof built) and pypi.tsv is 137,845 NOTRUN, so the floor counts no losses there; the daily floor leaves both out until they have a real JIT-off run. Also ft-packages jit hit 90k TIMEOUTs on 11-16; find why before re-adding.

