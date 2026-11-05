---
id: luc-j4mj
status: open
deps: []
links: []
created: 2026-11-05T08:11:41Z
type: task
priority: 3
assignee: Greg Wedow
---
# jit: retry units retired by the keeps-exiting rule

luc-jh5s retires a loop-free unit that leaves by side exit or exception on at least half its entries (min 50), and never retries it. A unit retired for exceptions that later stop happening stays interpreted for the rest of the process. Make retirement decay or back off, so a phase change gets another compile, without reopening the havlak exit storm (tools/jitcheck exits lane must stay green).

