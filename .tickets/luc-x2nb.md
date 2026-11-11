---
id: luc-x2nb
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Setting frame.f_lineno leaves f_lasti one or two instructions short of CPython

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_frame_diff fn frame_jump, about 500 cases over seeds 1 and 2 at 1,000 cases each; the accept/reject decisions all match.

```python
import sys
def v():
    x = 1
    y = 2
    return y
L = v.__code__.co_firstlineno
def tr(frame, ev, arg):
    if frame.f_code is v.__code__ and ev == 'line' and frame.f_lineno == L + 2:
        before = frame.f_lasti
        frame.f_lineno = L + 1
        print(before, frame.f_lasti)
        raise SystemExit
    return tr
sys.settrace(tr)
try: v()
except SystemExit: pass
sys.settrace(None)
```
CPython 3.14t:
```
build/repro/bytecode/frame_lineno_lasti.py:10: RuntimeWarning: assigning None to 1 unbound local
  frame.f_lineno = L + 1
6 2
```
Lucifer:
```
6 0
```


