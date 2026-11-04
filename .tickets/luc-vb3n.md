---
id: luc-vb3n
status: closed
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

**2026-11-04T12:48:50Z**

Review: fixed a shared list iterator that parked on the list's lock inside its own section (double decref of the list), [*L] reading past a shrunk array, PyList_Append without the lock (lost appends, NULL write into a list being sorted), PyList_GetItemRef taking plain refs, and plain stores of published items (now release, as listobject.c). List slices fill their own array. Best of 5, quiet host, seconds: L[i] pre-rjiv 0.254 / master 0.271 / branch 0.250; for x in L 0.180 / 0.206 / 0.179; slices 1.126 / 1.157 / 1.170; comprehension 0.081 / 0.113 / 0.092. threads_container_protocol times out under gc:1 on master as well.
