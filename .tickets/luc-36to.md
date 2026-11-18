---
id: luc-36to
status: open
deps: []
links: []
created: 2026-11-18T18:48:08Z
type: bug
priority: 3
assignee: Greg Wedow
---
# json_dumps +1.3% instructions, +3.5% cycles across the cq passes A-G

Accepted loss from luc-fhda..luc-um1n (f3879c4..720b666), measured on a core lease, 3 reps, master before vs after. Not attributed to a pass. Bisect over the seven commits by instructions, find which mechanism costs on json_dumps' shape (str building, dict iteration), fix in the mechanism.

