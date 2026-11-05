---
id: luc-kil1
status: open
deps: []
links: []
created: 2026-11-05T14:47:28Z
type: bug
priority: 2
assignee: Greg Wedow
---
# call path: bind keyword arguments by hash lookup, not by converting str subclasses

From luc-dawf checkpoint 7. merge_kwargs converts str-subclass keyword names to exact str (luc-h4gc), so test_capi.test_getargs kwargs_str_subclass passes but test_weird_str_subclass and test_weird_str_subclass2 fail; without the conversion the reverse. CPython satisfies both with one rule: parameter lookup goes through the dict's hash-based find, so a same-hash str subclass binds while a re-hashed lying subclass never matches (TypeError). Match CPython's rule in the kernel call path; both getargs pairs must pass (73/73 minus the subinterpreter test).

