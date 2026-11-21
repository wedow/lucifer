---
id: luc-fsue
status: closed
deps: []
links: []
created: 2026-11-19T20:12:43Z
type: bug
priority: 2
assignee: Greg Wedow
---
# web: Lucifer has no 8-CPU row in the daily

The 2026-11-19 daily web table has 3.14 and 3.14t at 8 CPUs but no Lucifer numbers (the summary CSV has no web.lucifer.8 keys). Find out whether the 8-CPU run fails, times out or is skipped, and fix the cause; the scaling row is the one that measures no-GIL efficiency.


## Notes

**2026-11-20T03:02:26Z**

2026-11-20: a codex run (cut off by its provider's filter) found the 8-CPU web run's Lucifer server dies in compiled code: coredump frames thread_main -> function_vectorcall -> lu_eval -> eval_frame -> lu_jit_enter -> compiled code -> call_generic, and in another thread lu_jit_direct <- function_vectorcall <- call_prepended <- lu_wrapper_descr_call. Check against e8b5a558 (direct-entry publish/kill) and luc-c69o's old redirect crash; read the cores with coredumpctl.

**2026-11-21T00:36:49Z**

Cause: luc-349f, fixed by 2148a50d (a returning unit's releases exited into its RETURN_VALUE with the value popped, so callers got NULL when another thread's write killed the unit). The 11-19 daily's server core (pid 274279, read with a rebuild of 7937512e) dies in the interpreter right after an exit: Py_INCREF(NULL) at LOAD_FAST of jinja2 Lexer.tokeniter's m just after 'm = regex.match(...)'; the lu_jit_direct thread was running normally. Repro without the web benchmark: 16 threads each parsing the app's Jinja templates in fresh Environments (as a fresh server's first 64 requests compile them), 15 runs x 20 iterations per lane: 7937512e 11/15 default, 3/15 jit=always; 2148a50d^ 11/15, 8/15; 2148a50d 0/15, 0/15; master c0c9eb75 0/15, 0/15. tests/kernel/jit_return_release_kill.py (from 2148a50d) is the kernel test. The 11-20 daily, the first with 2148a50d, has the 8-CPU row. e8b5a558 is not the cause (0/30 at its parent).
