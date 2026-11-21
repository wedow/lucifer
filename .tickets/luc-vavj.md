---
id: luc-vavj
status: closed
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 1
tags: [stdlib]
---
# _thread.RLock is not subclassable

class X(_thread.RLock) raises TypeError: type '_thread.RLock' is not an acceptable base type; CPython 3.14's RLock type takes subclasses. joblib's conftest does this, so all 3068 joblib ft-packages cases ERROR.


## Notes

**2026-11-21T07:39:11Z**

The Python RLock port rejected subclasses during class creation and initialized the mutex in __init__. Moving mutex setup to __new__ lets subclasses define their own initializer. Inherited context exit releases the lock directly, and repr uses the subclass name, matching CPython 3.14t. The new rlock_subclass kernel check failed on the base with the reported TypeError and passed with the fix. All eight targeted kernel checks passed in plain, jit, and jit-deopt.
