---
id: luc-obz9
status: closed
deps: []
links: []
created: 2026-11-10T10:36:14Z
type: bug
priority: 2
parent: luc-zelv
---
# repr(float) slower after kernel customization

k_repr_float 287 ns/op on master, 400-450 on luc-custom (three runs each, pinned core; CPython 231). Find which inline or arm made repr's path slower.


## Notes

**2026-11-10T13:03:29Z**

Fixed on luc-custom2 (55fee3a): float primitives were not marked as running no Python, so float.__repr__ counted as a call that could, and repr(x) could not inline past to_repr; it called call_special with its *args and **kwargs. 400k reprs: 3945M instructions before customization, 4382M on master, 3825M now.
