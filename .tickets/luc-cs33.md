---
id: luc-cs33
status: open
deps: []
links: []
created: 2026-11-07T21:12:05Z
type: task
priority: 2
assignee: Greg Wedow
parent: luc-vm3a
tags: [native, glm]
---
# simdutf in C: codecs, str/bytes conversion, base64

Pure C port of the simdutf subset Python needs: UTF-8 validate, UTF-8 <-> Latin-1/UCS-2/UCS-4 (PEP 393 kinds), UTF-16/32 with BOM/endianness, ASCII fast checks, base64 encode/decode (binascii, base64 module, incl. urlsafe and strict/non-strict validation). Fast path when input is valid; error handlers (strict, replace, ignore, surrogateescape, surrogatepass, backslashreplace, xmlcharrefreplace) fall back to the existing path from the first error position, with UnicodeDecodeError start/end/reason identical to 3.14t. Port simdutf's tests first, then differential + fuzz vs the oracle.

