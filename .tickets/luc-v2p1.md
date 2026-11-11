---
id: luc-v2p1
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Writing read-only type attributes gives different messages

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 20 cases over seeds 1 and 2 at 1,000 cases each.

```python
class A: pass
class B(A): pass
for n in ('__base__', '__mro__', '__bases__', '__basicsize__', '__flags__', '__dictoffset__', '__text_signature__', '__weakrefoffset__', '__itemsize__'):
    try: setattr(B, n, object)
    except Exception as e: print(n, type(e).__name__, e)
```
CPython 3.14t:
```
__base__ AttributeError readonly attribute
__mro__ AttributeError attribute '__mro__' of 'type' objects is not writable
__bases__ TypeError can only assign tuple to B.__bases__, not type
__basicsize__ AttributeError readonly attribute
__flags__ AttributeError readonly attribute
__dictoffset__ AttributeError readonly attribute
__text_signature__ AttributeError attribute '__text_signature__' of 'type' objects is not writable
__weakrefoffset__ AttributeError readonly attribute
__itemsize__ AttributeError readonly attribute
```
Lucifer:
```
__base__ AttributeError attribute '__base__' of 'type' objects is not writable
__mro__ AttributeError attribute '__mro__' of 'type' objects is not writable
__bases__ TypeError can only assign tuple to B.__bases__, not 'type'
__basicsize__ AttributeError attribute '__basicsize__' of 'type' objects is not writable
__flags__ AttributeError attribute '__flags__' of 'type' objects is not writable
__dictoffset__ AttributeError attribute '__dictoffset__' of 'type' objects is not writable
__text_signature__ AttributeError attribute '__text_signature__' of 'type' objects is not writable
__weakrefoffset__ AttributeError attribute '__weakrefoffset__' of 'type' objects is not writable
__itemsize__ AttributeError attribute '__itemsize__' of 'type' objects is not writable
```


