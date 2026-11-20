---
id: luc-fsue
status: open
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
