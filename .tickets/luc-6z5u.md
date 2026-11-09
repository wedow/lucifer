---
id: luc-6z5u
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# typing.NewType instances are not callable: _typing._idfunc binds as a method

CPython's typing.NewType sets __call__ = _idfunc, a builtin that does not bind. lib/modules/_typing.py defines _idfunc as a Python function, so calling a NewType passes self too: TypeError: _idfunc() takes 1 positional argument but 2 were given (websockets ExtensionName('permessage-deflate')). Fails pyperformance asyncio_websockets. Corpus: tools/bench/corpus.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: a builtin function stored as a special method is not a descriptor, so slot calls and the interpreter's call path no longer pass self. asyncio_websockets passes. tests/kernel/builtin_special_method.py.
