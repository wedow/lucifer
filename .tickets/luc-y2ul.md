---
id: luc-y2ul
status: open
deps: []
links: []
created: 2026-11-25T11:57:10Z
type: task
priority: 2
assignee: Greg Wedow
parent: luc-ia92
---
# pattern-matching opcodes leave compiled code (MATCH_CLASS and family)

From the item 2 work (01a9cae0): xdsl's SSAValue.get retires at pc 15 (MATCH_CLASS), 20K interpreted entries per run; MATCH_CLASS is 1.4% of xdsl's bytecode, and the other MATCH_* opcodes and GET_LEN run there too. The evaluator supports none of the pattern-matching opcodes, so any function with a class pattern leaves compiled code at it on every entry.

Build: MATCH_CLASS, MATCH_MAPPING, MATCH_SEQUENCE, MATCH_KEYS and GET_LEN in the evaluator through the kernel's helpers (the interpreter's own), with the isinstance half of MATCH_CLASS folded under the class's version where the subject's type is known. Firing check first: which of them run, and how often, across the corpus programs.

Acceptance: a count check that a function with a class pattern and one with a mapping pattern stay compiled; behaviour tests against the oracle for __match_args__ changed mid-loop, a subject class with a custom __instancecheck__ metaclass, positional and keyword sub-patterns, missing attributes; xdsl instructions before and after.

