---
id: luc-ajdn
status: open
deps: []
links: []
created: 2026-11-06T21:49:51Z
type: task
priority: 1
assignee: Greg Wedow
---
# JIT: exceptions as control flow inside compiled code

From luc-jp30: ~580k error exits in str.translate (stdlib mix) and ~220k per Flask run (next_item, generator.__next__ StopIteration). Handle raise/catch within compiled code (or make the kernel paths not raise) generally; measure Flask in-process routes and stdlib mix.

