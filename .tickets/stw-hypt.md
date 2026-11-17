---
id: stw-hypt
status: open
deps: []
links: []
created: 2026-11-17T19:50:56Z
type: bug
priority: 2
assignee: Greg Wedow
---
# unraisable report from a raising __del__ prints only the header

Lucifer prints 'Exception ignored while calling deallocator ...' with no traceback and no exception line; CPython prints both.

