---
id: luc-fc6q
status: open
deps: []
links: []
created: 2026-11-04T12:56:45Z
type: bug
priority: 3
parent: luc-h4gc
---
# pycache_prefix option ignored

PYTHONPYCACHEPREFIX sets sys.pycache_prefix, but the -X pycache_prefix=PATH option leaves it None; CPython honours both.

