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


## Notes

**2026-11-17T21:28:08Z**

luc-6exm's turn rule costs genshi rebuild churn: XMLSerializer.__call__@116 is built 7 times (3 before) as each build reveals new hot sites in the filter generators it inlines; compile +240 ms, code +3.4 MB, net instructions still -1.2%. A cost model that marks the sites a build will reveal, or caps rebuilds by what they earned, would remove it.
