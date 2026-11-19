---
id: luc-fsue
status: open
deps: []
links: []
created: 2026-11-19T20:12:43Z
type: bug
priority: 2
assignee: Greg Wedow
---
# web: Lucifer has no 8-CPU row in the daily

The 2026-11-19 daily web table has 3.14 and 3.14t at 8 CPUs but no Lucifer numbers (the summary CSV has no web.lucifer.8 keys). Find out whether the 8-CPU run fails, times out or is skipped, and fix the cause; the scaling row is the one that measures no-GIL efficiency.

