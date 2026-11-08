---
id: luc-ptxc
status: closed
deps: []
links: []
created: 2026-11-07T20:06:32Z
type: feature
priority: 1
---
# _sre: specialize hot patterns into generated Python matchers

Roadmap item 8a. Compile each hot pattern's code list to Python source for a specialized matcher function (opcodes unrolled, literals/charsets as constants, branches as control flow, repeats as loops), cached on the Pattern and compiled by the JIT like any Python function. The generic _match interpreter stays the fallback for unsupported opcodes and the oracle for differential tests. Specialize only after adaptive use count so compile-heavy programs do not slow down.

## Acceptance Criteria

regex_v8, regex_effbot, cli.markdown faster than master under bench; regex_compile not slower; test_re oracle rows do not regress; differential tests between specialized and generic paths pass; five gates green.

