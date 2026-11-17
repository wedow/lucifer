---
id: luc-l39r
status: closed
deps: []
links: []
created: 2026-11-17T14:35:07Z
type: bug
priority: 2
---
# BytesIO read/readline check closed before converting the size

fuzzdiff pyio_base_diff-s2/726 lost in every mode since 7fe2e02: a size whose __index__ closes the BytesIO must raise ValueError (CPython converts the size first, then CHECK_CLOSED); lucifer returned b''. read had the same order and a TypeError message unlike CPython's.

