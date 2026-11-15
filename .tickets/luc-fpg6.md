---
id: luc-fpg6
status: closed
deps: []
links: []
created: 2026-11-15T02:59:27Z
type: bug
priority: 1
assignee: Greg Wedow
---
# threads_dict_unpack_race prints 'bad 0 3' on master

tests/kernel/threads_dict_unpack_race.py failed 6/6 runs on a clean origin/master build (with 8c8ce61's agent, under host load), printing 'bad 0 3' rather than timing out: likely a real free-threading bug in dict unpacking (** / update) racing with concurrent mutation. Earlier floors passed it, so check whether it is load-dependent; reproduce in JIT off, always and deopt:7; compare with CPython 3.14t.


## Notes

**2026-11-15T05:03:52Z**

Not a Lucifer bug: 'bad 0 3' is the correct output (0 bad unpackings, 3 readers). The oracle timed out (>120 s): cp314t spends the run in lock handoffs (sched_yield/futex, <1 s CPU in 30 s) and slows 20-50x when CPUs are oversubscribed. Lucifer passed 48/48 runs (off/default/always/deopt, idle and loaded). Test rewritten in a83cb42: writer runs until readers finish, 1000 reads; oracle 0.1 s idle, <3.3 s loaded; still crashes a build without the unpacking critical sections.
