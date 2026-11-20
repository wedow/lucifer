---
id: luc-4pg3
status: closed
deps: []
links: []
created: 2026-11-19T20:12:43Z
type: bug
priority: 1
assignee: Greg Wedow
---
# pyperformance-warm: sqlalchemy_declarative output differs from CPython

The 2026-11-19 daily (built from origin/master at 06:00) reports corpus wrong for pyperformance-warm.sqlalchemy_declarative; the same program passes in the cold pyperformance suite. The difference is more warm-up, so suspect a JIT miscompile after deeper optimization. Reproduce with the warm suite's loop counts, then bisect against JIT off and jit-deopt.


## Notes

**2026-11-20T23:11:17Z**

Fixed by c0c9eb75: the unit kept an adopted closure generator's function (guard + constant), so the first flush's closure cells kept its UOWTransaction and a Person in the weak identity map; reduced to tests/kernel/gen_adopt_closure_release.py.
