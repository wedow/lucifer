---
id: luc-b5lx
status: closed
deps: []
links: []
created: 2026-11-19T00:05:50Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: []= on Python classes through the kernel's setitem

Gap report 2026-11-18 mechanism #3, second stage. STORE_SUBSCR on anything but list[int] called H_setitem -> kernel setitem -> type_lookup -> call_special -> __setitem__ on every store (scimark's array('d') and Array2D stores). It now goes through lu_ev_kernel_arms/lu_ev_kernel_op with LU_RETURN_DISCARD (inlined frames that discard their value), so the store folds to the class's __setitem__, in line where the budget allows.

## Acceptance Criteria

jitcheck lane 'operators' covers []= on two classes; scimark loses instructions on a core lease; floor quick, kernel lanes and check-jit pass.


## Result

Landed on luc-ka5h. Firing (scimark, 3+3 pyperformance values, perf instructions:u): h_setitem 0.49% -> 0.01%,
lu_call_kernel 0.65% -> 0.02%, type_lookup 1.43% -> 0.45%; lane `operators` covers []= on two classes (flat).
Stack totals are in luc-ka5h; scimark 0.808 of master's instructions (3 reps) is both stages. scimark's hot loops
(FFT_transform_internal, LU_factor) still leave getitem/setitem calls out of line over the inline budget: array
element access itself is gap-report mechanism #5.
