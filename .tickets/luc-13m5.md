---
id: luc-13m5
status: open
deps: []
links: []
created: 2026-11-15T10:38:25Z
type: bug
priority: 2
---
# the callee table compares a borrowed func_globals pointer

runtime/jit/jit.c (the callee table, around the func_globals comparisons): an entry keeps a borrowed func_globals pointer and later compares it with a live function's, so a freed globals dict whose address was reused (sooner now that dicts are kept for reuse per carrier) can match a different namespace. Hold a reference, or compare by a version that cannot be reused. Found by the race review of the free lists.

