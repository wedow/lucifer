---
id: luc-tk2r
status: open
deps: []
links: []
created: 2026-11-15T10:38:25Z
type: bug
priority: 1
---
# an idle scheduler carrier runs lu_qsbr_process with no task attached

runtime/thread/sched.c run_loop: an idle scheduler carrier processes its QSBR queue with no task attached, so delayed releases (_PyObject_XDecRefDelayed items) can run deallocs while another thread's stop-the-world borrows ob_tid for gc_refs, and can push onto the carrier's free lists while a full collection takes them (runtime/interp/collector.c). Deallocs must run attached, or the queue must be processed only by an attached task. Found by the race review of the free lists.

