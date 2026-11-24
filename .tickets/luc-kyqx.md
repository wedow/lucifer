---
id: luc-kyqx
status: open
deps: []
links: []
created: 2026-11-24T08:06:50Z
type: task
priority: 1
parent: luc-orn7
tags: [perf, jit]
---
# jit: a raise caught by the caller stays in compiled code (no side exit per raise)

Measured (luc-mopl diagnosis, 2026-11-24): django_template takes 404K side exits per 20 steady values at chain.__next__ (kernel iterator written in Python), RAISE_VARARGS of StopIteration: the tier compiles RAISE_VARARGS as a side exit (the interpreter raises), so every exhausted chain costs an exit, the rest of the frame interpreted and a re-entry; escape_filter and chain.__next__ show 3.2% of cycles in the interpreter. Other rows: genshi 790 error exits, deepcopy 287 (_keep_alive, a KeyError caught by the caller).

Task: a raise whose exception is caught by the caller that called the raising code (the for loop's StopIteration, a dict lookup's KeyError) completes in compiled code: the unit raises into its own handler or returns the exception to a compiled caller, with no exit. Overlaps luc-3s7n (exceptions as control flow): settle which ticket builds which part before starting.

Acceptance: a test program whose loop exhausts a Python-written iterator by StopIteration takes no exits per iteration (jitcheck growth lane flat in N); django_template steady side exits at chain.__next__ zero.

