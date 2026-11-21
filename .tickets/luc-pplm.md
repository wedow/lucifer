---
id: luc-pplm
status: closed
deps: []
links: []
created: 2026-11-21T10:59:31Z
type: bug
priority: 2
tags: [list, gc]
---
# list: del/pop of the last item fails on a list built from gc.get_referents of the kernel heap

Found while counting the booted kernel heap for the boot-snapshot design (11-21, master 1ddf3ecf, plain and jit lanes alike, so not the JIT). A depth-first walk from builtins, _lucifer and sys through gc.get_referents, with the stack a plain list, reaches a state where len(stack) is 2781 and stack[-1] returns (repr: the str 'MISSING'), but 'del stack[-1]' raises 'IndexError: list assignment index out of range' and stack.pop() 'pop index out of range'; appending items one by one instead of extend changes nothing. The objects just before it are generic_getattr's code and a tuple (False, <object object at 0x55...>) whose second item is a static C object, so a kernel-internal sentinel (the MISSING / NULL stand-in) probably reaches Python through gc.get_referents and the list storage treats it as a hole. Repro (bin/lucifer -S): import gc, sys, builtins, _lucifer; seen=set(); stack=[builtins,_lucifer,sys]; then loop: o=stack[-1]; del stack[-1] (raises here); skip seen ids; stack += gc.get_referents(o). CPython's walk finishes. Fix: get_referents must not expose internal sentinels (CPython's Py_VISIT skips NULL), and find why the list cannot drop the item.


## Notes

**2026-11-21T11:03:15Z**

Same walk: the dict 'seen' (id -> object) has len 29572 and iterates 29572 keys, but seen.values() and list(seen.values()) yield 1834. So the leaked sentinel acts as end-of-container in dict values iteration as well as a hole in list deletion: any container that stores it is corrupted. Root cause is the leak itself.

**2026-11-21T12:56:47Z**

The traversal visitor in `runtime/interp/gc.c` returned `LU_MISSING` as a Python value. The list primitive deleted the item and returned the sentinel, which the kernel interpreted as an invalid index. Dict value iteration treated the sentinel as exhaustion. The visitor now skips it by identity. `tests/kernel/gc_referents.py` reproduced the `IndexError` before the fix. After the fix, the new test and ten related kernel tests matched CPython 3.14t in each of the plain, jit and jit-deopt lanes.
