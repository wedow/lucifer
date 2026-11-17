---
id: luc-1ejp
status: closed
deps: []
links: []
created: 2026-11-17T08:39:00Z
type: task
priority: 2
---
# types: set_slot by slot index, not a strcmp scan

update_slots calls set_slot ~80 times per class; each scanned all ~85 slot names with strcmp (7.2% of import_stdlib instructions). Resolve the name once (slot_index).

