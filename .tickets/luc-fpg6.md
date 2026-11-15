---
id: luc-fpg6
status: open
deps: []
links: []
created: 2026-11-15T02:59:27Z
type: bug
priority: 1
assignee: Greg Wedow
---
# threads_dict_unpack_race prints 'bad 0 3' on master

tests/kernel/threads_dict_unpack_race.py failed 6/6 runs on a clean origin/master build (with 8c8ce61's agent, under host load), printing 'bad 0 3' rather than timing out: likely a real free-threading bug in dict unpacking (** / update) racing with concurrent mutation. Earlier floors passed it, so check whether it is load-dependent; reproduce in JIT off, always and deopt:7; compare with CPython 3.14t.

