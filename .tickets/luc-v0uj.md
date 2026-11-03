---
id: luc-v0uj
status: open
deps: [luc-nsmh]
links: []
created: 2026-11-03T01:38:06Z
type: task
priority: 1
tags: [validation]
---
# Run Lucifer's libtest and subprocesses against Lucifer's own stdlib and interpreter, not the oracle's

Review luc-roth findings:
- --source vendored puts all of CPython Lib/ ahead of Lucifer's stdlib.
- numpytest/ftcorpus put the oracle venv bin first on PATH.
- pypi lets 'python' by name resolve to the host interpreter.
Depends on DESIGN.md's stdlib layout.

## Acceptance Criteria

a fake interpreter whose stdlib differs from the oracle's is tested with its own modules (binary-level check). Subprocess python/f2py resolve to the interpreter under test. No vacuous stdlib-walking tests.

