---
id: luc-h1ym
status: open
deps: []
links: []
created: 2026-11-05T14:40:17Z
type: bug
priority: 3
assignee: Greg Wedow
---
# kernel builtins: __text_signature__ where CPython has none, and positional-only markers

luc-34ha derives __text_signature__ for kernel builtins from their code. 25 builtins (max, min, math.log, time.gmtime, ...) now report a signature where CPython has None, so inspect.signature returns one instead of raising ValueError. Port functions written as def add(a, b) give ($module, a, b) where CPython gives ($module, a, b, /), and accept keyword arguments CPython rejects. Fix: positional-only parameters in the port sources where CPython's are, and an opt-out for builtins CPython leaves unsigned; compare all kernel builtins' __text_signature__ against the oracle in a test.

