---
id: luc-1dgu
status: open
deps: []
links: []
created: 2026-11-19T11:09:07Z
type: task
priority: 2
---
# str.encode and bytes.decode look codec names up in one probe

pickle/unpickle rows: every encode/decode normalized the encoding name (lowercase copy, two str_replace scans) and scanned tuples of names and handlers, per call (memmem, find_in, prim_str_replace in the profiles). A dict of the spellings as given and as normalized answers in one probe; names are read as exact str, so a str subclass's methods never run (C reads its text).

