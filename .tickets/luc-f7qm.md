---
id: luc-f7qm
status: open
deps: []
links: []
created: 2026-11-07T21:46:28Z
type: feature
priority: 2
---
# _sre specialization: loops whose iterations backtrack

The specializer (luc-ptxc) leaves a REPEAT with more than one iteration and a choice point inside its body to the interpreter, e.g. (?:a|ab)*c, (<(.|\s)+>), the fractions and format-spec regexes of the stdlib, markdown's HTML attribute patterns. About 5% of the patterns the regex and cli corpus programs compile. Emitting MAX_UNTIL as a generated function that recurses per iteration would cover them, but Python recursion bounds the iteration count; it needs either a fallback to the interpreter on RecursionError or an explicit stack of continuations in the generated code. Measure how much of the corpus time those patterns take first.

