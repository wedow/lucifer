---
id: luc-wov9
status: closed
deps: []
links: []
created: 2026-11-10T02:35:04Z
type: task
priority: 2
parent: luc-zelv
---
# Container intrinsics: list slice store, list unpack without a tuple copy, str[i] without boxing

Design stage 5. fannkuch (2.57x CPython) is perm[:k+1] = perm[k::-1] through h_store_slice -> setitem -> slice_bounds; k_slice_assign 563 ns against 169; k_list_unpack 70 against 67; k_str_index 96 against 25.


## Notes

**2026-11-10T13:03:36Z**

Done on luc-custom2 (947c982): slice stores into an exact list from a tuple or list splice natively, in the interpreter and compiled code, also when CPython folds a constant slice into STORE_SUBSCR; str[i] reads its code point in line and str_len is a load. Instructions: p[:5] = p[4::-1] 6075 -> 2021 (CPython 1754), fannkuch 17.6G -> 9.0G, t[i] == 'a' 1134 -> 406 (369). List unpack was already near parity (810 vs 765) and is unchanged.
