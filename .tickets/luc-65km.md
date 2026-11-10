---
id: luc-65km
status: open
deps: []
links: []
created: 2026-11-10T02:35:04Z
type: task
priority: 2
parent: luc-zelv
---
# Iteration fusion: MISSING returns route to the loop exit so step tuples stay virtual

FOR_ITER over zip/enumerate/dict.items now runs the kernel step in line (luc-zelv stage 4 part), but the step's result is a phi of MISSING and the item tuple, and the 'is MISSING' test makes the tuple real (zip 137 ns/item against CPython 54, enumerate 154 against 60). Route constant-MISSING returns of frames inlined at a FOR_ITER (and tail calls in them) straight to the loop exit, or teach escape analysis that a fresh object is never identical to a constant. Then inline the inner next_item steps for list/tuple iterators.


## Notes

**2026-11-10T05:41:04Z**

Done in luc-zelv stage 3-4: constant-MISSING returns of FOR_ITER step frames (and their tail calls) route to the loop exit, so the zip tuple is virtual; inner list/tuple iterator steps run without a frame (seqiter_step); kernel code may recur once. zip 342 -> ~116 ns/item (CPython 54), enumerate 219 -> 158 (60), dict.items 96 -> 68 (40). Left: GUARD_FRAMES and refcount churn per step (5 frames guards, 10 decrefs per zip step), enumerate's field_get/field_set prim calls and its index box per step.

**2026-11-10T13:03:36Z**

Progress on luc-custom2 (24ba429): field_get/field_set on a known layout are helpers with the offset found at build time, not the primitive's type table and boxed index. enumerate 1658 -> 853 instructions per item, zip 1222 -> 941 (CPython 735, 652). Left: the zip/enumerate object is real in loops compiled at their back-edge (made before entry), each field access takes the object's critical section, and enumerate boxes its index per step; virtual iteration objects (design 4.1) are not built yet.
