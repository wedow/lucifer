---
id: luc-giw1
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# _warnings is a stub: no _acquire_lock/_release_lock, no skip_file_prefixes or stacklevel checks, no onceregistry version

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff fns warn.*, about 60 cases over seeds 1 and 2 at 1,000 cases each.

```python
import warnings
print(hasattr(warnings, "_acquire_lock"), hasattr(warnings, "_filters_mutated"))
import _warnings
print(sorted(n for n in dir(_warnings) if not n.startswith("__")))
```
CPython 3.14t:
```
True True
['_acquire_lock', '_defaultaction', '_filters_mutated_lock_held', '_onceregistry', '_release_lock', '_warnings_context', 'filters', 'warn', 'warn_explicit']
```
Lucifer:
```
timeout: failed to run command ‘bin/lucifer’: Permission denied
```

```python
import warnings
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter("always")
    warnings.warn("x")
    print(len(w), w[0].message)
try:
    warnings.warn("x", skip_file_prefixes=(1,))
except Exception as e: print(type(e).__name__, e)
try:
    warnings.warn("x", stacklevel=2**64)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
1 x
TypeError Found non-str 'int' in skip_file_prefixes.
OverflowError Python int too large to convert to C ssize_t
```
Lucifer:
```
timeout: failed to run command ‘bin/lucifer’: Permission denied
```

```python
import warnings, io, sys
warnings.simplefilter("once")
sys.stderr = io.StringIO()
warnings.warn_explicit("b", UserWarning, "f", 1)
sys.stderr = sys.__stderr__
print(sorted(map(repr, warnings.onceregistry)))
print(sorted(map(repr, warnings.__dict__.get("onceregistry", {}).values())))
```
CPython 3.14t:
```
["'version'", "('b', <class 'UserWarning'>)"]
['1', 'True']
```
Lucifer:
```
["('b', <class 'UserWarning'>)"]
['1']
```

Needs a port of Python/_warnings.c.


