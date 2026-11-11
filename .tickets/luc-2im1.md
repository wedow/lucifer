---
id: luc-2im1
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Error messages do not apply CPython's %.200s truncation to type names

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff and typeobj_diff, about 50 cases over seeds 1 and 2 at 1,000 cases each.

```python
N = type('L' * 300, (), {})
try: N(x=1)
except Exception as e: print(len(str(e)), str(e)[-30:])
N = type('é' * 150, (), {})
try: N(x=1)
except Exception as e: print(len(str(e)), repr(str(e)[-30:]))
try: eval('o' + 'é' * 150)
except BaseException as e: print(type(e).__name__, len(str(e)), repr(str(e)[-20:]))
```
CPython 3.14t:
```
221 LLLLLLLLL() takes no arguments
121 'ééééééééé() takes no arguments'
NameError 122 "éééé' is not defined"
```
Lucifer:
```
321 LLLLLLLLL() takes no arguments
171 'ééééééééé() takes no arguments'
UnicodeDecodeError 76 'expected end of data'
```

Lucifer also raises UnicodeDecodeError when the cut falls inside a UTF-8 sequence.


