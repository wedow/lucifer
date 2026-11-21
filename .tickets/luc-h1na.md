---
id: luc-h1na
status: open
deps: []
links: []
created: 2026-11-21T23:45:39Z
type: bug
priority: 3
tags: [codecs]
---
# codecs: incomplete trailing surrogate sequence is an error in partial UTF-8 decoding

codecs.utf_8_decode(b'\xed\xa0', errors, False) returns ('', 0) on CPython 3.14 for every handler (the two bytes wait for more input). Lucifer raises or replaces them: _utf8_tail in lib/modules/_codecs.py finds the pending tail with a strict decode, which rejects ED A0 as an invalid continuation. Same for any prefix (b'q\xed\xa0'). Found by the luc-e8os differential run; it predates that branch.

