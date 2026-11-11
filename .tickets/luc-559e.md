---
id: luc-559e
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.read/readline with sizes beyond ssize_t return data instead of OverflowError

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_callback_diff, pyio_state_diff, pyio_share_diff, pyio_text_diff, about 130 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO(b"abc")
for name in ("read", "readline"):
    for n in (2**63, 2**64):
        try: print(name, n, getattr(f, name)(n))
        except Exception as e: print(name, n, type(e).__name__, e)
try: f.seek(2**62); f.write(b"x" * 8)
except BaseException as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
read 9223372036854775808 OverflowError cannot fit 'int' into an index-sized integer
read 18446744073709551616 OverflowError cannot fit 'int' into an index-sized integer
readline 9223372036854775808 OverflowError cannot fit 'int' into an index-sized integer
readline 18446744073709551616 OverflowError cannot fit 'int' into an index-sized integer
MemoryError 
```
Lucifer:
```
read 9223372036854775808 b'abc'
read 18446744073709551616 b''
readline 9223372036854775808 b''
readline 18446744073709551616 b''
MemoryError 
```


