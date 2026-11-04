---
id: luc-wm18
status: closed
deps: []
links: []
created: 2026-11-04T09:32:04Z
type: bug
priority: 1
assignee: Greg Wedow
---
# KeyboardInterrupt from a back-edge poll escapes its enclosing try

A KeyboardInterrupt raised at a loop back-edge signal poll is not caught by the enclosing try/except; test_threading hangs on test_print_exception_gh_102056 because of it. Present on master. Found in luc-c702. The pending-signal check must raise at an instruction boundary covered by the exception table, as CPython's eval breaker does.


## Notes

**2026-11-04T12:57:10Z**

fixed: back-edge poll error attributed to jump; interrupt_main tripped twice under handoff; default_int_handler now C; PyDLL calls stay attached. test_threading hang gone (209 PASS, 7 FAIL, 1 ERROR left).
