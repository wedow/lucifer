---
id: luc-tbcj
status: closed
deps: []
links: []
created: 2026-11-04T09:05:20Z
type: bug
priority: 2
assignee: Greg Wedow
---
# thread dying with an exception ends in RecursionError report

After a thread dies with an exception, the excepthook output ends in 'RecursionError: <exception str() failed>' and the program's final print is lost. Oracle prints the traceback and continues. Found in luc-rjiv review.


## Notes

**2026-11-05T00:51:34Z**

Not reproducible on master: threads dying with plain, chained, SystemExit, custom-__str__ and recursion-limit exceptions, under plain, handoff, jit and gc:1 lanes, print the oracle's traceback and the program continues. Regression test added. Likely a symptom of the lock-free container corruption fixed since (luc-ojw0).
