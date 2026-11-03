---
id: luc-0t2o
status: open
deps: []
links: []
created: 2026-11-03T09:29:21Z
type: task
priority: 3
tags: [kernel]
---
# builtin function, function, method and descriptor types lack __call__ in their type dicts, so collections.abc.Callable's subclass hook fails for len

isinstance(len, collections.abc.Callable) is False: _check_methods walks type(len).__mro__ dicts for __call__ and finds none (builtin_function_or_method, function, method, method-wrapper, method_descriptor, wrapper_descriptor). Installing a slot wrapper must not route native calls through a Python thunk. Found by tests/kernel/import_io.py work (luc-qiwk).

