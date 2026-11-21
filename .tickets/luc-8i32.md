---
id: luc-8i32
status: closed
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

**2026-11-21T01:27:42Z**

Fixed by 297f55f2: kernel frames take words from a per-task room sized to the carrier's C stack instead of the recursion count; C-to-interpreter calls check the C stack; compiled code is not entered below the JIT stack reserve; Py_EnterRecursiveCall checks the C stack as in 3.14. Max nesting under a 512 KiB stack before -> after (plain lane; oracle in parens): repr list 260 -> 976 (3338), repr dict 347 -> 1077 (2730), == list 209 -> 585 (2310), == tuple segfault -> 585 (2730), json.dumps list 518 -> 1171 (2730), deepcopy 498 = 498 (498). tests/kernel/deep_container_recursion.py (400-deep repr/str/==/hash/deepcopy/json at limits 1000 and 80, user recursion through repr/sorted/map at the limit, 100000-deep raising RecursionError) fails before and passes after in plain, jit, jit-deopt; 31/32 neighbours pass in each lane (null_item_repr needs _testlimitedcapi, not built by make all).
