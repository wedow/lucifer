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

**2026-11-04T12:49:18Z**

From the luc-vb3n review: threads_signal_sort failed once in handoff:3 with interrupt_main's KeyboardInterrupt landing in 'while t.is_alive()' outside its try. Check whether this is the back-edge bug or a test race.

**2026-11-04T12:57:10Z**

fixed: back-edge poll error attributed to jump; interrupt_main tripped twice under handoff; default_int_handler now C; PyDLL calls stay attached. test_threading hang gone (209 PASS, 7 FAIL, 1 ERROR left).

**2026-11-04T13:10:00Z**

The threads_signal_sort case: an asynchronous interrupt can land at the back edge after POP_JUMP_IF_TRUE, which the compiler (like CPython's) leaves outside the try, so that program's rare failure is a test race CPython shares, not this bug.
