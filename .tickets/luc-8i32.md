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


## Notes

**2026-11-20T22:19:27Z**

2026-11-20: a codex run (cut off by its provider's filter) wrote an iterative explicit-stack repr for list/tuple/dict in lib/kernel/_format.py (99 lines); not landed: it fixes repr alone. Cause to fix instead: kernel methods written in Python spend 2-3 Python frames per nesting level against sys.getrecursionlimit(), where CPython's C builtins count against the C stack, not the Python limit, so == / hash / copy / repr on deep containers all hit it. Fix in the mechanism (kernel frames bounded like C frames, by stack depth) and test repr, ==, hash and str of a 400-deep list/tuple/dict against CPython. The codex test and patch are kept outside the repo.
