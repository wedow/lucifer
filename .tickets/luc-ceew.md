---
id: luc-ceew
status: open
deps: []
links: []
created: 2026-11-04T09:05:20Z
type: task
priority: 3
assignee: Greg Wedow
---
# global dict serial counter: measure contention

Every new or resized dict table does an atomic add on next_serial_counter (luc-rjiv). Measure under many-thread dict creation; if it shows, hand out serial blocks per carrier. Also: 31-bit serial wrap past 2^32 entries corrupts the stamp (theoretical).

