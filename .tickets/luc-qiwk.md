---
id: luc-qiwk
status: open
deps: []
links: []
created: 2026-11-03T09:00:47Z
type: task
priority: 1
tags: [runtime, kernel]
---
# K1o gap: import io, abc and typing need GenericAlias, UnionType and _types

The frozen io, abc, _py_abc, _weakrefset and _collections_abc modules are in the build, but import io stops in types.py: list[int] is not subscriptable (types.GenericAlias and __class_getitem__ on list, dict, tuple, set, frozenset, type), int | str has no UnionType, the _types module port is absent, and exc.__traceback__.tb_frame is needed by types.FrameType. abc falls back to _py_abc, which imports _weakrefset, which imports types. Acceptance: import io, abc, collections.abc and typing succeed under bin/lucifer -I -S, and a tests/kernel program that uses io.StringIO, io.TextIOWrapper(io.BytesIO()) and an abc.ABC subclass matches the oracle. Relates luc-48jh.

