---
id: luc-eog8
status: open
deps: []
links: []
created: 2026-11-22T22:46:34Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# attrs: field kinds in the class layout, so an attribute load or store is a few instructions

Measured (exact single-step counts on master, luc-bpmh's table): a compiled int attribute load is 21 instructions and a store 35; they are 84 of the 167 instructions per iteration in the read-4-attributes idiom and 70 of 141 in store-2-attributes. PyPy's whole iteration is 35 and 23.

Why: an inline attribute is self-describing (a value word that is an object, absent, or an unboxed marker, plus a raw word), so every load re-derives what it holds: the in-dict flag, the marker compare, and when the value word holds an object (anything the interpreter or C stored, which is every instance made before its __init__ was compiled) the type compare, the immortal and owner checks, the int's tag, digit and sign. A store decides the same things under the object's lock.

Mechanism: record per class and name what every instance holds there, in the shared names table: unseen, int (fits a machine word), float, or object. Then:
- For an int or float kind the raw word is always valid whoever stored it: the interpreter and C store paths write the raw word beside the object they store; compiled stores write the raw word and the marker.
- A compiled load under the class version guard it already has is a presence compare on the value word and a load of the raw word (object kind: the value word itself). A compiled store of the kind is the raw word, the marker, and a release of the old value word only if it held an object.
- A store of another kind generalizes the name's kind to object once, for good, and invalidates the units that depended on it (the same dependency machinery as other class facts). Instances stay self-describing, so nothing is rewritten.
- The in-dict flag becomes a class fact too (no instance of this class has moved its values to a dict), invalidated by the first move, instead of a test per access.
Depends on luc-6b0l stage 1 for the lock-free store sequence.

Acceptance: instructions per int attribute load <= 4 and per store <= 8 on the idiom rows read-4-attributes, store rows, monomorphic call, and the Bounce-shaped loop; instances made by the interpreter and by compiled code read at the same cost; a kernel test where one instance later stores a str into an int field (and a float, and deletes it, and moves to a dict via vars()) in all three lanes.

