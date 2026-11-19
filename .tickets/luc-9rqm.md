---
id: luc-9rqm
status: open
deps: []
links: []
created: 2026-11-19T11:29:55Z
type: task
priority: 2
---
# str primitives lower in line on an operand whose type is learned

json_loads: _json's _skip (and any unit entered with the str as a parameter: direct calls, _sre's matchers) called prim:str_ord out of line, boxing the index each character: lu_ev_prim lowered str_ord/str_item/str_len/1-char str_cmp only for operands whose type was already known in the unit. They now learn the operand's type from the call's profile (guarded), as typed_get does.

