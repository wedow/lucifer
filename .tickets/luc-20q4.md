---
id: luc-20q4
status: open
deps: []
links: []
created: 2026-11-19T11:22:09Z
type: task
priority: 2
---
# an instance attribute with an immortal class value behind it loads in line

pickle/unpickle rows: BytesIO.read's self._buffer and _IOBase.closed's self.__closed have a class value behind the instance's (_buffer = None, __closed = False). The interpreter's LOAD_ATTR_INSTANCE_VALUE keeps that value as descr for compiled code, but load_as fell back to H_load_attr (lu_getattr_native -> lu_attr_lookup -> type_lookup, under critical sections) for every such site. INLINE_LOAD now answers the class value where the instance word is empty, when that value is immortal; the version guard keeps it the class's.

