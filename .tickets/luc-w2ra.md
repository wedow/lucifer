---
id: luc-w2ra
status: open
deps: []
links: []
created: 2026-11-21T05:59:49Z
type: task
priority: 1
assignee: Greg Wedow
---
# startup: importing site costs ~91M instructions (-c pass 180M -> 271M)

23e0d389 made startup import site, as CPython does. Median user-space instructions for -c pass rose from 179.9M to 271.1M (+51%); -S stays at 179.9M. site.py and _sitebuiltins are frozen, so the cost is in what site does (imports, stat/listdir of site-packages, .pth handling, sysconfig/user-site paths). Attribute it with perf/icount, compare with CPython 3.14t's site cost on the same host, and fix the general mechanism it exposes (import machinery, os/stat paths, kernel boot), never site.py itself.

