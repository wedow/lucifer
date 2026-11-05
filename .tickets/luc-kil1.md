---
id: luc-kil1
status: closed
deps: []
links: []
created: 2026-11-05T14:47:28Z
type: bug
priority: 2
assignee: Greg Wedow
---
# call path: bind keyword arguments by hash lookup, not by converting str subclasses

From luc-dawf checkpoint 7. merge_kwargs converts str-subclass keyword names to exact str (luc-h4gc), so test_capi.test_getargs kwargs_str_subclass passes but test_weird_str_subclass and test_weird_str_subclass2 fail; without the conversion the reverse. CPython satisfies both with one rule: parameter lookup goes through the dict's hash-based find, so a same-hash str subclass binds while a re-hashed lying subclass never matches (TypeError). Match CPython's rule in the kernel call path; both getargs pairs must pass (73/73 minus the subinterpreter test).


## Notes

**2026-11-05T16:27:35Z**

test_capi.test_getargs 70/73 -> 72/73 (left: test_gh_119213, subinterpreter). The oracle has three rules, not one: a Python function matches by identity then the keyword's __eq__ (initialize_locals), a C kwargs dict by hash and __eq__, kwnames by text (find_keyword). Callers now keep str-subclass keys; kernel functions match by text and store the text in **kwargs. Kernel builtins that forward **kwargs (type call to __init__, dict(**), functools.partial) still pass exact strs where the oracle keeps the subclass; int() with a lying key binds (oracle: tp_call dict lookup refuses).
