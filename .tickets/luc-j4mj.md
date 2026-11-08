---
id: luc-j4mj
status: closed
deps: []
links: []
created: 2026-11-05T08:11:41Z
type: task
priority: 3
assignee: Greg Wedow
---
# jit: retry units retired by the keeps-exiting rule

luc-jh5s retires a loop-free unit that leaves by side exit or exception on at least half its entries (min 50), and never retries it. A unit retired for exceptions that later stop happening stays interpreted for the rest of the process. Make retirement decay or back off, so a phase change gets another compile, without reopening the havlak exit storm (tools/jitcheck exits lane must stay green).


## Notes

**2026-11-08T04:41:12Z**

Fixed on luc-policy (luc-xboo): retirement is a death like any other, so the entry waits and is retried with exponential backoff; the jitcheck exits lane stays green and AWFY side exits are flat with iterations. tests/kernel/jit_retry.py: jitcheck retry lane.
