---
id: luc-srqu
status: open
deps: []
links: []
created: 2026-11-21T09:50:43Z
type: bug
priority: 2
tags: [fork, threads]
---
# kernel fork_threads times out about 1 run in 85 (plain), not traced to the lock commits

The 11-21 daily (3c94548c) failed fork_threads in the plain lane: lucifer timeout, no output. On master 6127bd00, 85 plain runs: 1 timeout (10 kernelcheck runs on a loaded host), 0 in 75 more (15 kernelcheck, 60 direct). At 474944ca, before the RLock/stream-lock commits 62aaa6bf and 7003c94f: 0 in 75. The rate is too low to attribute to those commits; nothing in fork_threads takes an RLock directly (threading.Event's Condition does). Hang site not captured: next step is a loop with faulthandler.dump_traceback_later or gdb attach on a timeout to see which thread waits on what (the children's join, the parent's waitpid, or the workers' Lock).

