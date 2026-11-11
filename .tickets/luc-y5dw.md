---
id: luc-y5dw
status: open
deps: []
links: []
created: 2026-11-11T21:47:10Z
type: task
priority: 3
assignee: Greg Wedow
tags: [perf]
---
# pattern that always overflows recursion rebuilds frames each call

_sre generated matchers fall back to the generic interpreter on RecursionError, but nothing remembers that a pattern always overflows; (?:a|ab)*c on 'ab'*20000 builds ~recursion-limit frames on every call before rerunning. Remember per pattern (or per subject length class) and go straight to the interpreter.

