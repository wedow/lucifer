---
id: luc-lssm
status: open
deps: []
links: []
created: 2026-11-05T03:11:18Z
type: bug
priority: 2
---
# Cells: LOAD_DEREF/STORE_DEREF race, and the translator folds cell contents borrowed

Left by luc-m0na. The interpreter's LOAD_DEREF increfs cell->ob_ref without a try, and STORE_DEREF/DELETE_DEREF replace it with a plain store and decref, so two threads storing to one cell (nonlocal) can release a value twice, and a reader can take a freed one. cp314t reads with a try-incref and a locked fallback (_PyCell_GetStackRef) and stores under the cell's critical section (PyCell_SetTakeRef). The JIT translator's LOAD_DEREF fold (ssa_build.c) reads PyCell_GET borrowed before taking the constant, and h_cell_load increfs without a try. Fix both as the slot members were (lu_member_exchange, lu_try_xget_ref) with a binary-level test of threads writing a shared nonlocal.

