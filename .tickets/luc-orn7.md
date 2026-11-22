---
id: luc-orn7
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: epic
priority: 0
tags: [perf, jit]
---
# perf: per-operation cost to PyPy (the floor under every program)

Finding (2026-11-22, master 720b9d61). Whole-program profiles rank functions; they cannot show a cost that sits on every load, store, call and allocation. Measured per operation instead: 25 ordinary idioms, steady state (difference of two loop counts, one pinned core), cycles per inner iteration, Lucifer / PyPy 3.11 / CPython 3.14t:

| idiom | Lucifer | PyPy | CPython |
|---|---|---|---|
| read 4 int attributes and add | 61 | 10 | 280 |
| store 2 int attributes | 117 | 7 | 153 |
| store an object attribute | 72 | 9 | 99 |
| store a float attribute | 64 | 10 | 236 |
| monomorphic method call | 48 | 19 | 230 |
| call over 3 receiver classes | 178 | 21 | 430 |
| object made and dropped | 20 | 8 | 884 |
| object made and kept in a list | 190 | 23 | 620 |
| for v in xs: s += v | 26 | 1 | 175 |
| xs[j] = xs[j] + 1 | 118 | 5 | 183 |
| d[k] = d.get(k, 0) + 1 (str keys) | 530 | 116 | 485 |
| s += d[k] (int keys) | 173 | 33 | 189 |
| "-".join(parts) | 411 | 118 | 541 |
| f"{i}:{j}" | 694 | 195 | 1116 |
| startswith/strip/find | 599 | 95 | 682 |
| sum(x * x for x in xs), per item /100 | 268 | 39 | 202 |
| [x + 1 for x in xs if x & 1], per item /100 | 106 | 21 | 139 |
| for a, b in pairs | 44 | 8 | 161 |
| f(j, c=i) | 530 | 12 | 431 |
| f(*args, **kw) forwarding | 1418 | 8 | 1556 |
| try: d[k] / except KeyError (half raise) | 1509 | 37 | 593 |
| isinstance(b, (C, B)) | 210 | 8 | 506 |
| float accumulate | 28 | 8 | 252 |
| closure call | 66 | 4 | 248 |
| linked-list walk | 31 | 9 | 150 |

No row is within 2.5x of PyPy. Three causes, read off the IR dump and the machine code:

1. The object protocol under the JIT is free-threaded CPython's. Every attribute and list store takes and releases the object's lock byte (two lock cmpxchg). A build with those two emitters stubbed out (measurement only): store 2 int attributes 124 -> 60 cycles, object attribute 68 -> 30, float attribute 66 -> 39, a Bounce-shaped method loop 178 -> 107. Reference counts go up and down with an owner-thread compare on every pointer read; ints read from containers are heap objects; allocation and release run through generic C.
2. The code generator spends about 7-12 machine instructions per IR instruction on IR that is already close to ideal (the Bounce-shaped loop: method inlined, ints unboxed, 301 instructions per iteration against PyPy's 96; for v in xs: s += v, 11 IR instructions, 73 machine instructions against 17). Values are reloaded from frame slots at each use, compare then branch is six instructions, slow paths sit inside the hot path, the same flag is tested twice.
3. Common idioms leave compiled code: keyword calls, star forwarding, an exception caught in the same unit (a deopt per raise), a closure cell read (a C call), a call over three classes, isinstance against a tuple.

Real programs (14 pyperformance rows, steady state as values 4-23 minus the first 3, sampled cycles): compiled units 41% (28% program and stdlib, 13% kernel), C runtime reached from them 41% (object 11%, allocation/release/refcount/collector 10%, prims 8%, locks 2.5%, rest), JIT compiler 11%, interpreter 7%. No C function is above 2.6%. Warm-up is not the cause: django_template, deepcopy, richards and chameleon are flat from about the tenth value to the 200th at 2.2x CPython / 12x PyPy, 2.2x / 10x, 0.26x / 9x, 1.7x / 8x.

The children are the general mechanisms. The acceptance metric for each is the idiom table (rows it names, cycles and instructions per iteration), then the whole-run cycle rule on the pyperformance rows it moves. A row is done within 1.5x of PyPy.

