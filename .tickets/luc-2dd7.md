---
id: luc-2dd7
status: open
deps: []
links: []
created: 2026-11-23T06:48:53Z
type: bug
priority: 2
assignee: Greg Wedow
tags: [jit, warmup]
---
# jit: classes made at run time pay the whole warmup again on every call

Shape: a function that defines a class and then loops over its instances (closures that define a class, namedtuple and dataclass factories called in loops, test fixtures).

```python
def run(n):
    class Node:
        def __init__(self, nxt):
            self.next = nxt
            self.v = 1
    head = None
    for _ in range(100):
        head = Node(head)
    s = 0
    for _ in range(n):
        p = head
        while p is not None:
            s += p.v
            p = p.next
    return s

for _ in range(3):
    run(3000)
```

Observed (exact single-step trace of the idiom row while_attr, and -X lucifer_jit_stats): 2405 instructions per outer iteration, 2158 of them in C (lu_jit_deopt, eval_frame, lu_jit_enter, type_lookup, the attribute-load helpers). Every outer iteration the inner loop's unit (entered at the while header, run@57) leaves by a side exit at the outer loop's JUMP_BACKWARD, which it does not compile; the interpreter runs the outer loop once and re-enters the inner unit. Each call compiles new units: their GUARD_VERSION guards carry the previous call's Node version tag as a constant, so the units of call k fail on call k+1's class and are rebuilt. Per call the inner unit is entered 150 to 1000 times (one side exit each) before the outer loop's unit (run@52) is built, entered once, and runs the rest of the call well. With one call, 940 of 3000 outer iterations go through the side exit. The trace's 2405 measured the third call at n=60, which never gets past that warmup.

Questions: (1) a unit that leaves at an enclosing loop's back edge on every run should get that loop compiled with it (or count toward its compile) instead of waiting for the outer counter; (2) units for a class made anew per call could guard what the code depends on (the class's layout: shared names, indices, kinds) rather than its identity or version, so the next call's class with the same layout reuses them.

