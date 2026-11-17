---
id: luc-9gvb
status: closed
deps: []
links: []
created: 2026-11-17T09:47:27Z
type: bug
priority: 2
---
# interp: no property specialization under __getattr__; listdir(bytes) gives bytes

A property whose getter raised AttributeError stopped falling back to the class's __getattr__ once its site specialized (LOAD_ATTR_PROPERTY called the getter directly); CPython declines the specialization there. os.listdir(bytes) returned str names. Found by tests/kernel/jit_property_getter.py and os_path_encode.py.

