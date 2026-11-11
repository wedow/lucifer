---
id: luc-f09m
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# unicodedata._ucnhash_CAPI capsule is missing

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): unicodeobj_diff cannot load (all 2,000 cases) over seeds 1 and 2 at 1,000 cases each: it reaches the name lookup the namereplace handler uses through the capsule.

```python
import unicodedata
print(hasattr(unicodedata, '_ucnhash_CAPI'))
print('\N{LATIN SMALL LETTER A}', 'a'.encode('ascii', 'namereplace'), '\xe9'.encode('ascii', 'namereplace'))
```
CPython 3.14t:
```
True
a b'a' b'\\N{LATIN SMALL LETTER E WITH ACUTE}'
```
Lucifer:
```
False
a b'a' b'\\N{LATIN SMALL LETTER E WITH ACUTE}'
```


