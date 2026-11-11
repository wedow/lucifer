---
id: luc-f7qm
status: closed
deps: []
links: []
created: 2026-11-07T21:46:28Z
type: feature
priority: 2
---
# _sre specialization: loops whose iterations backtrack

The specializer (luc-ptxc) leaves a REPEAT with more than one iteration and a choice point inside its body to the interpreter, e.g. (?:a|ab)*c, (<(.|\s)+>), the fractions and format-spec regexes of the stdlib, markdown's HTML attribute patterns. About 5% of the patterns the regex and cli corpus programs compile. Emitting MAX_UNTIL as a generated function that recurses per iteration would cover them, but Python recursion bounds the iteration count; it needs either a fallback to the interpreter on RecursionError or an explicit stack of continuations in the generated code. Measure how much of the corpus time those patterns take first.


## Notes

**2026-11-11T18:16:31Z**

Done on branch luc-regex: such a repeat is a generated function per iteration (MAX_UNTIL/MIN_UNTIL mirrored, marks saved as the interpreter saves them); RecursionError reruns the interpreter for that call. Every pattern the docutils, sphinx, black, markdown, chameleon, pygments and regex corpus programs compile now generates. docutils regex instructions per loop 97.5G -> 20.6G (CPython 2.5G), total 6.8x -> 3.1x CPython's; sphinx 6.1x -> 3.8x; cli.markdown 10.9x -> 7.3x.
