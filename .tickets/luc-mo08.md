---
id: luc-mo08
status: open
deps: []
links: []
created: 2026-11-06T16:45:21Z
type: task
priority: 2
assignee: Greg Wedow
---
# JIT: boxed int store to the root frame every iteration; error exits

From luc-n05c profile: a loop accumulating a large int boxes and stores it to the root frame slot every iteration (~35% of a plain call-loop bench). Flask has ~110k exceptions crossing compiled code via error exits. Profile and fix by mechanism.

