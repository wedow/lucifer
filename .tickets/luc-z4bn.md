---
id: luc-z4bn
status: open
deps: []
links: []
created: 2026-11-07T01:22:27Z
type: task
priority: 2
assignee: Greg Wedow
---
# Remaining exceptions on Flask hot paths

From luc-ajdn: generic-getattr AttributeError still raised with kwargs through kernel Python (~8 per Flask request, mostly caught by Jinja Environment.getattr) - set name/obj in C as CPython does. Handlers in inlined user frames cannot catch in compiled code (need a real frame object for the traceback). Kernel-unit raises (int(), float(), bad list index) still error-exit and rebuild the root frame. next(it) without default and tuple_iterator.__next__ raise StopIteration ~3x per request.

