---
id: luc-7vga
status: open
deps: []
links: []
created: 2026-11-19T12:00:49Z
type: task
priority: 3
---
# json scanner: recursive descent runs out of inline budget

json_loads after the 11-19 libmods lane: _parse_array/Scanner.__call__ units are 90-200 KB; _skip, _match_number, _scanstring, dict.setdefault and the recursive _scan_once are left as calls ('over budget' / 'recursive' in LUCIFER_JIT_DEBUG), 688 k compiled calls per 9 k loads. Tried: one loop over an explicit stack of open containers (no recursion, same errors, differential-tested against the C scanner): the loop unit met values of every type at one site, left on every entry and retired 8 levels; entries rose 55 k -> 1.39 M. Needs the JIT side: budget by what a path executes rather than code size, or polymorphic value sites that do not leave; then revisit.

