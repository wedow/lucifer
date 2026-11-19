---
id: luc-8i32
status: open
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: bug
priority: 3
---
# repr of a list nested 400 deep raises RecursionError

len(str(json.loads('[' * 400 + '1' + ']' * 400))) raises RecursionError: maximum recursion depth exceeded under Lucifer; CPython 3.14 prints it (its C repr limit is the C stack, tens of thousands deep). The kernel's list/dict repr recurse in Python, several frames per level, against the Python recursion limit. Found writing tests/kernel/json_scan_random.py.

