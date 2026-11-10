---
id: luc-g0um
status: open
deps: []
links: []
created: 2026-11-10T02:35:04Z
type: task
priority: 2
parent: luc-zelv
---
# Iteration fusion: generator-expression consumers (sum, set, tuple, str.join, list) fused into one loop

Design kernel-customization section 4 items 3-4: sum(genexpr) 116 ns/item against CPython 42, set(genexpr) 300 against 98, tuple(genexpr) 127 against 66, join(genexpr) 97 against 41, list(map(lambda)) 383 against 66. Needs the consumer's next_item to resume the genexpr in line when its body calls Python (luc-s4lm) and the generator object kept virtual.


## Notes

**2026-11-10T13:03:36Z**

Progress on luc-custom2 (77ff10a, 2520186), instructions per item in a compiled caller (master -> branch, CPython): sum(genexpr) 1241 -> 948 (537), ','.join(genexpr) 954 -> 612 (493), set(genexpr) 3283 -> 3077 (1099), tuple(genexpr) 1222 -> 1071 (768), list(map(lambda)) 3524 -> 3538 (813). sum and join run the generator's body in the consumer's loop. Left: the yielded float is boxed per item (15% of sum's), each step of the genexpr's own list iterator takes a critical section (11%), set() reaches update_iterable too deep to inline, tuple() and map() still call; allocating loops also leave at scheduled collections (luc-ar8c).
