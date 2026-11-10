---
id: luc-obz9
status: open
deps: []
links: []
created: 2026-11-10T10:36:14Z
type: bug
priority: 2
parent: luc-zelv
---
# repr(float) slower after kernel customization

k_repr_float 287 ns/op on master, 400-450 on luc-custom (three runs each, pinned core; CPython 231). Find which inline or arm made repr's path slower.

