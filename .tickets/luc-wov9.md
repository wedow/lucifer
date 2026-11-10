---
id: luc-wov9
status: open
deps: []
links: []
created: 2026-11-10T02:35:04Z
type: task
priority: 2
parent: luc-zelv
---
# Container intrinsics: list slice store, list unpack without a tuple copy, str[i] without boxing

Design stage 5. fannkuch (2.57x CPython) is perm[:k+1] = perm[k::-1] through h_store_slice -> setitem -> slice_bounds; k_slice_assign 563 ns against 169; k_list_unpack 70 against 67; k_str_index 96 against 25.

