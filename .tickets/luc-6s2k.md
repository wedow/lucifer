---
id: luc-6s2k
status: open
deps: []
links: []
created: 2026-11-03T02:21:42Z
type: bug
priority: 2
tags: [validation]
---
# pypi harness: pin PATH so host tools (e.g. ImageMagick display) cannot change pillow test ids; redact the environment path even without --env

## Acceptance Criteria

a host PATH containing a fake 'display' leaves pillow's collected ids unchanged; a run without --env commits no environment path (binary-level checks in tools/pypi/check.py)

