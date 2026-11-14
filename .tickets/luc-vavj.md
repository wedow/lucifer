---
id: luc-vavj
status: open
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 1
tags: [stdlib]
---
# _thread.RLock is not subclassable

class X(_thread.RLock) raises TypeError: type '_thread.RLock' is not an acceptable base type; CPython 3.14's RLock type takes subclasses. joblib's conftest does this, so all 3068 joblib ft-packages cases ERROR.

