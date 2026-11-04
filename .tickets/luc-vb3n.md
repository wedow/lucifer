---
id: luc-vb3n
status: open
deps: []
links: []
created: 2026-11-04T09:05:20Z
type: feature
priority: 1
assignee: Greg Wedow
---
# lock-free list reads: arrays that carry their capacity

luc-rjiv put a lock on every list subscript and iteration step: L[i] +20%, for-x-in-list +16% versus before. CPython 3.14t reads lists lock-free because the item array carries its own allocated size (_PyListArray), so a reader can bounds-check a QSBR-retired array without the list's lock. Give our list storage the same shape and drop the read lock. Acceptance: threads_container_protocol and handoff lanes stay green; L[i] and iteration hot loops return to master-before-rjiv timings or better on a quiet host.


## Notes

**2026-11-04T10:53:20Z**

Built on branch luc-x9ab (misnamed): head 155af12; Opus review running.
