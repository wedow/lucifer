---
id: luc-izr3
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# StringIO(newline=...) validates newline as a Python str, not as a C string

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_diff fn stringio_c, 80 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io
for nl in ('\r\0', '\n\0', '\0', '\ud800', '\r\n\0', '\0\n', 'x'):
    try:
        print(repr(nl), repr(io.StringIO("a\r\nb", nl).getvalue()))
    except Exception as e:
        print(repr(nl), type(e).__name__, e)
```
CPython 3.14t:
```
'\r\x00' 'a\r\rb'
'\n\x00' 'a\r\nb'
'\x00' 'a\r\nb'
'\ud800' UnicodeEncodeError 'utf-8' codec can't encode character '\ud800' in position 0: surrogates not allowed
'\r\n\x00' 'a\r\r\nb'
'\x00\n' 'a\r\nb'
'x' ValueError illegal newline value: 'x'
```
Lucifer:
```
'\r\x00' ValueError illegal newline value: '\r\x00'
'\n\x00' ValueError illegal newline value: '\n\x00'
'\x00' ValueError illegal newline value: '\x00'
'\ud800' ValueError illegal newline value: '\ud800'
'\r\n\x00' ValueError illegal newline value: '\r\n\x00'
'\x00\n' ValueError illegal newline value: '\x00\n'
'x' ValueError illegal newline value: 'x'
```

CPython encodes newline to UTF-8 (a surrogate raises UnicodeEncodeError) and reads it up to the first NUL before validating.


