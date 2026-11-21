---
id: luc-26l7
status: closed
deps: []
links: []
created: 2026-11-21T09:52:03Z
type: task
priority: 2
assignee: Greg Wedow
---
# startup: class creation's _set_names and kernel type install run as interpreted Python

luc-w2ra attribution (11-21): per class steady cost 266k instructions vs CPython's 89k. After fc0a83e8 moved update_slots to a primitive, the next run-once pieces are type.__new__'s _set_names (~20 LOC, 1-3M at startup) and the kernel class install kernel_type.install / _kernel_value (~150 LOC, ~86k bytecodes at boot). Move them to primitives mirroring CPython's type_new where that removes interpreted work, keeping __set_name__ and __init_subclass__ semantics and error messages identical. Measure per-class cost and -S -c pass.


## Notes

**2026-11-21T12:49:54Z**

Kernel bootstrap now installs class-body entries in one native pass using
the existing descriptor constructors and slot table. Heap class creation
uses a native namespace snapshot and hook pass following CPython's
`Objects/typeobject.c` (`type_new_set_names`). Successful hooks run in
namespace order before `__init_subclass__`. Hook call failures gain the
CPython note on the original exception.

The `class_set_name` check failed on d1718e17 because the note was missing.
It passes with the fix. The targeted gate passed 12 checks in each of
plain, jit and jit-deopt.

The table reports medians of five pinned runs per command and interpreter,
counting user-space instructions with `PYTHONHASHSEED=0`. Both Lucifer
builds used the same checkout and executable path. The class workload ran
1,000 `class C: pass` statements under `-S -c`. Per-class cost is the
median of the paired instruction totals minus the `pass` cost, divided
by 1,000.

| Interpreter | `-S -c pass` | 1,000 empty classes | Instructions per class |
| --- | ---: | ---: | ---: |
| Lucifer d1718e17 | 146,447,457 | 364,395,083 | 217,941 |
| Lucifer 93d8e8c7 | 128,868,847 | 338,758,233 | 209,872 |
| CPython 3.14.10t | 36,085,677 | 111,712,573 | 75,634 |

Startup used 12.0% fewer instructions. The measured empty-class cost
was 3.7% lower on this workload.
