---
id: luc-uu0y
status: open
deps: []
links: []
created: 2026-11-24T19:18:43Z
type: bug
priority: 2
assignee: Greg Wedow
parent: luc-orn7
---
# collection is triggered from resident memory, so its timing is not reproducible

should_collect_mem_usage (runtime/interp/gc.c) decides whether to collect from the process's resident memory (VmRSS). Transparent huge pages move resident memory from run to run, so collection timing differs between runs of the same binary with address randomisation off and one thread; with it the fill of the int free lists and so one jitcheck count (box_int fallbacks reads 32, 33 or 34 over ten runs on master), and when finalizers run. Trigger collection from a deterministic heap measure (bytes or objects the allocator handed out since the last collection) so that counted lanes are reproducible and collection does not depend on the kernel's page policy. Found while making the payback ledger's decisions reproducible (luc-1kz0).

