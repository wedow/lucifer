---
id: luc-bfwp
status: closed
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: feature
priority: 2
tags: [libtest]
---
# libtest: _tkinter is not built (2178 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. _tkinter is missing, so test_tkinter 1004, test_idle 687, test_ttk 373, test_turtle 56, test_tcl 45 and test_ttk_textonly 13 skip. Build Modules/_tkinter.c against the system Tcl/Tk like the other stdlib extension modules (Makefile STDLIB_EXTS); the source is not vendored yet.


## Notes

**2026-11-10T05:18:05Z**

Built from Modules/_tkinter.c and tkappinit.c against the system Tcl/Tk 8.6 (luc-clibs). Headless (no DISPLAY), Lucifer equals CPython 3.14t: test_tcl 45, test_ttk_textonly 13, test_turtle 56, test_idle 300 (3.14t 304), test_tkinter and test_ttk skip entirely on both (1014 and 398). The committed 3.14t record had a display (test_tkinter 1004, test_ttk 373, test_idle 687); those need a display run to compare.
