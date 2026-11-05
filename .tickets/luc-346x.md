---
id: luc-346x
status: open
deps: []
links: []
created: 2026-11-05T22:34:07Z
type: task
priority: 2
assignee: Greg Wedow
---
# perf: lock-free iterator step with CPython's never-clear it_seq

luc-jeec2 dropped the unlocked iterator step to close a use-after-free (it_seq cleared on exhaustion). Cost ~9% Bounce, ~8% Storage. CPython keeps it_seq until dealloc; adopting that would restore the unlocked step safely, at the cost of exhausted iterators holding their sequence longer.

