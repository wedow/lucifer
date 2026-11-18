---
id: luc-el8t
status: closed
deps: []
links: []
created: 2026-11-18T04:07:30Z
type: task
priority: 1
---
# jit: stores into an instance the code just made take no lock

Gap report 2026-11-18 mechanism #1 (plain stores). The old middle end (f9c8284^, ssa_opt.c fresh_stores, INS_FRESH) stored into an instance the code had just made without its lock and without publishing, and gave an inline store of a known class its room so a new name went in line; f9c8284 lost both (emit.c fresh = 0, imm2 never set). Restore them in the evaluator: lu_ev_instance_real's stores while nothing has published since H_new_object, and store_room for an owner of known class.


## Result

Landed. Steady instructions per iteration, before -> after on a core lease: CD 0.947, Havlak 0.960, Bounce 0.971,
pyperformance raytrace 0.924 and chaos 0.922 (instances made in compiled code that escape); a loop that appends
two-field instances to a list: 4.44 G -> 3.64 G instructions. AWFY cycles geomean 0.979 for the pass.
