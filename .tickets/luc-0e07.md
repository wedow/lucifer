---
id: luc-0e07
status: open
deps: []
links: []
created: 2026-11-03T15:56:37Z
type: bug
priority: 1
tags: [test, threads]
---
# Flakes under host load: threads_basic and threads_signal_sort failed once under LANE=handoff (0/4 on rerun); unicodedata_module times out near its limit (~98 s alone)

Treat as a regression until explained (noise is a regression). Capture the failing diff with a loop under load, find whether a test depends on timing or the runtime has a race; shrink unicodedata_module or make it sample code points so it runs well inside the timeout.

