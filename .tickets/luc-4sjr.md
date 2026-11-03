---
id: luc-4sjr
status: open
deps: []
links: []
created: 2026-11-03T03:52:06Z
type: chore
priority: 3
tags: [runtime]
---
# blockcheck: preprocess os.c (or scan included runtime headers) so a macro or inline helper calling a blocking function cannot escape the lint

## Acceptance Criteria

a planted #define in a runtime header used by os.c, expanding to read(), fails make check-blocking

