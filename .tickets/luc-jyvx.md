---
id: luc-jyvx
status: open
deps: []
links: []
created: 2026-11-16T11:32:48Z
type: bug
priority: 2
assignee: Greg Wedow
---
# startup runs two GCs; defer collection until after boot

python_startup_no_site 3.6x CPython (224M vs 42M instructions). Two gc collections during boot are ~12% of startup.

