---
id: luc-6j3k
status: closed
deps: []
links: []
created: 2026-11-17T18:28:05Z
type: bug
priority: 2
---
# qsbr: a retire runs finalizers in the middle of the retiring change

lu_qsbr_retire processed the carrier's queue itself once 254 items waited, so delayed releases (replaced __dict__s) ran finalizers inside the change that retired the item, e.g. a list resize under the list's lock; the list then changed under the change in progress. Repro: tests/kernel/qsbr_retire_finalizer.py (48 threads swap __dict__s holding __del__ objects, grow and clear a shared list, collect cycles) segfaults in lu_list_splice's item decref on master 8 of 20 runs. Fix: a retire only asks the next poll to process (every DEFERRED_LIMIT retires already does).


## Notes

**2026-11-17T18:28:10Z**

Fixed: retire no longer processes the queue inline. qsbr_retire_finalizer.py: master 8/20 runs crash, 0/20 with the fix; stress (48 writers, 400 iterations, MALLOC_PERTURB_=165, cycles) master 3-4/12 crash, 0/12.
