---
id: luc-gxuy
status: open
deps: []
links: []
created: 2026-11-16T14:32:30Z
type: bug
priority: 3
---
# signal.set_wakeup_fd warn_on_full_buffer and write errors; marshal allow_code

set_wakeup_fd accepts warn_on_full_buffer but ignores it, and a failed write to the wakeup fd is silent (CPython reports it through a pending call, Modules/signalmodule.c report_wakeup_write_error). marshal.load/loads/dumps accept allow_code but the reader always allows code objects.

