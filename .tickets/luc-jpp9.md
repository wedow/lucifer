---
id: luc-jpp9
status: closed
deps: []
links: []
created: 2026-11-17T05:59:28Z
type: bug
priority: 2
assignee: Greg Wedow
---
# drain delayed frees at thread exit and in gc.collect

Race review of luc-ftfix: a thread's QSBR queue/orphans are processed only past 127 retires or a 254-item queue (qsbr.c:194). CPython processes the queue as a thread exits (_PyMem_AbandonDelayed) and drains all queues in gc. Probe: 50 threads each replace a __dict__ holding a __del__ object, join, gc.collect(): CPython 50 finalized, Lucifer 37-38. Orphans unbounded in principle. Fix: process_orphans() from the collector while attached, or set should_process in lu_qsbr_unregister. Also strengthen tests/kernel/qsbr_idle_carrier.py, which passes on master (does not witness luc-tk2r).


## Notes

**2026-11-17T18:28:19Z**

Fixed: the collector takes every queue and the orphans while the world is stopped (lu_qsbr_take_all), merging each object (merge_queued_object), so what was queued dies in that collection; covers exited threads' orphans too, so no unregister change. tests/kernel/qsbr_idle_carrier.py now counts finalizers after the collect loop (1592) and after 50 threads each drop one and gc.collect() (50): master printed 1589-1591 and 1-11.
