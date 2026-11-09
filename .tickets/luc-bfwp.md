---
id: luc-bfwp
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: feature
priority: 2
tags: [libtest]
---
# libtest: _tkinter is not built (2178 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. _tkinter is missing, so test_tkinter 1004, test_idle 687, test_ttk 373, test_turtle 56, test_tcl 45 and test_ttk_textonly 13 skip. Build Modules/_tkinter.c against the system Tcl/Tk like the other stdlib extension modules (Makefile STDLIB_EXTS); the source is not vendored yet.

