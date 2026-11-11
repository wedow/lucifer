---
id: luc-gwet
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# int(), float() and complex() treat ASCII 0x1C-0x1F as whitespace

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_str_diff fn intstr (17 cases), longobj_complex_diff fns cx_parse and cx_parse_float (7 cases) over seeds 1 and 2 at 1,000 cases each.

```python
for s in ("\x1c5", "\x1f5", " \x1c5", "5\x1c", "\x855", "\x0b5", "\x1d5 "):
    try: print(repr(s), int(s))
    except Exception as e: print(repr(s), type(e).__name__, e)
```
CPython 3.14t:
```
'\x1c5' ValueError invalid literal for int() with base 10: '\x1c5'
'\x1f5' ValueError invalid literal for int() with base 10: '\x1f5'
' \x1c5' ValueError invalid literal for int() with base 10: ' \x1c5'
'5\x1c' ValueError invalid literal for int() with base 10: '5\x1c'
'\x855' 5
'\x0b5' 5
'\x1d5 ' ValueError invalid literal for int() with base 10: '\x1d5 '
```
Lucifer:
```
'\x1c5' 5
'\x1f5' 5
' \x1c5' 5
'5\x1c' 5
'\x855' 5
'\x0b5' 5
'\x1d5 ' 5
```

```python
for s in ['\x1f0', '\x1c1', '1\x1c', '　1\x1f', '\xa01\x1c']:
    for f in (float, complex):
        try:
            print(f.__name__, repr(s), f(s))
        except Exception as e:
            print(f.__name__, repr(s), type(e).__name__, e)
```
CPython 3.14t:
```
float '\x1f0' ValueError could not convert string to float: '\x1f0'
complex '\x1f0' ValueError complex() arg is a malformed string
float '\x1c1' ValueError could not convert string to float: '\x1c1'
complex '\x1c1' ValueError complex() arg is a malformed string
float '1\x1c' ValueError could not convert string to float: '1\x1c'
complex '1\x1c' ValueError complex() arg is a malformed string
float '\u30001\x1f' ValueError could not convert string to float: '\u30001\x1f'
complex '\u30001\x1f' ValueError complex() arg is a malformed string
float '\xa01\x1c' ValueError could not convert string to float: '\xa01\x1c'
complex '\xa01\x1c' ValueError complex() arg is a malformed string
```
Lucifer:
```
float '\x1f0' ValueError could not convert string to float: '\x1f0'
complex '\x1f0' ValueError complex() arg is a malformed string
float '\x1c1' ValueError could not convert string to float: '\x1c1'
complex '\x1c1' ValueError complex() arg is a malformed string
float '1\x1c' ValueError could not convert string to float: '1\x1c'
complex '1\x1c' ValueError complex() arg is a malformed string
float '\u30001\x1f' 1.0
complex '\u30001\x1f' (1+0j)
float '\xa01\x1c' 1.0
complex '\xa01\x1c' (1+0j)
```

CPython strips only " \t\n\v\f\r" below 128 and uses Py_UNICODE_ISSPACE above 127; str.isspace() is true for 0x1C-0x1F.


