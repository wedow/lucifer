---
id: luc-l0vb
status: closed
deps: []
links: []
created: 2026-11-10T21:46:54Z
type: task
priority: 2
tags: [perf, jit]
---
# IOBase.closed and its __closed load generically in compiled code

On luc-custom4, BytesIO.closed (a property of _IOBase) and _IOBase__closed (a class attribute False, shadowed in an instance's dict once it is closed) are 50k-60k generic attribute loads in 3 loops of pickle_pure_python and unpickle_pure_python, though the interpreter specializes both sites (LOAD_ATTR_PROPERTY, LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES). The getter's site is shared by every IO class (FileIO's instances are closed, so the dict-lacks guard fails there), and the compiled site goes generic instead of keeping an arm per class.


## Notes

**2026-11-11T04:12:10Z**

Fixed (see the luc-l0vb branch commit): __closed is an inline name with a class default, so instances that never closed fell to the generic path; compiled code now answers them with the class's value (kept beside the word by the specializer). BytesIO.closed's getter, where it did not inline, is called directly. pickle 373M -> 332M, unpickle 205M -> 142M instructions per loop.
