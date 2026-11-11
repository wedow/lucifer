---
id: luc-hnyz
status: open
deps: []
links: []
created: 2026-11-11T04:20:08Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# int.from_bytes of a bytes subclass ignores __bytes__ (the 3.15 behaviour)

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): mathint_diff fn from_bytes_obj, 1 case over seeds 1 and 2 at 500 cases each.

```python
class B(bytes):
    def __bytes__(self):
        return b'\x7f\x7f'
print(int.from_bytes(B(b'\x01\x02\x03'), 'little'))
```
CPython 3.14t:
```
32639
```
Lucifer:
```
197121
exit 0
```


