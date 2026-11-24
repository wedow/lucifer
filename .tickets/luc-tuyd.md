---
id: luc-tuyd
status: open
deps: []
links: []
created: 2026-11-24T20:52:00Z
type: bug
priority: 3
assignee: Greg Wedow
parent: luc-orn7
---
# argument-count error messages of Python-written builtins differ from CPython's

dict.get({}) with no key raises TypeError: missing 1 required positional argument: 'key' in Lucifer; CPython says: get expected at least 1 argument, got 0. The same holds for every builtin method written in Python in the kernel whose CPython counterpart is a C function using the argument clinic's messages (expected N arguments, got M; takes no keyword arguments; takes exactly one argument). Survey the core types' methods against CPython for the message on too few, too many and keyword arguments, decide the general mechanism (the kernel function declares its C-style signature and the binder produces CPython's wording), and test it across the methods of str, list, dict, set, tuple, int, float, bytes. Found while testing special methods of several kinds (luc-2r1l).

