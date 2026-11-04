---
id: luc-tbcj
status: open
deps: []
links: []
created: 2026-11-04T09:05:20Z
type: bug
priority: 2
assignee: Greg Wedow
---
# thread dying with an exception ends in RecursionError report

After a thread dies with an exception, the excepthook output ends in 'RecursionError: <exception str() failed>' and the program's final print is lost. Oracle prints the traceback and continues. Found in luc-rjiv review.

