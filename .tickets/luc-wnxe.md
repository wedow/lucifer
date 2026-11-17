---
id: luc-wnxe
status: open
deps: []
links: []
created: 2026-11-17T03:15:52Z
type: task
priority: 2
assignee: Greg Wedow
---
# replace the hot-site 64-calls-per-entry threshold with a code-size cost model

8d58c71/437737d take a refused call site in line once it reaches 8x the unit's IR size in calls and >=64 calls per entry. The 64 was picked from observed spread (Json <=22, go 107) to stop recursive-descent units copying each other (Json code 7.4 -> 46-50 MB without it). Replace with a cost model weighing benefit against code size and compile time; no fixed constants tuned to benchmarks.

