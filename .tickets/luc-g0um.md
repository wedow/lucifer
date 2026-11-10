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

