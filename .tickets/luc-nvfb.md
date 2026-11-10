---
id: luc-nvfb
status: open
deps: []
links: []
created: 2026-11-09T04:58:34Z
type: bug
priority: 1
---
# libtest: segfaults that kill whole Lib/test modules

Cases that segfault Lucifer's worker in the full Lib/test run (2026-11-09), each zeroing its module: test_inspect TestClassesAndFunctions.test_getfullargspec_builtin_func_no_signature; test_pydoc TestDescriptions.test_bound_builtin_classmethod_unrepresentable_default; test_code_module TestInteractiveConsole.test_syntax_error; test_descr ClassPropertiesAndMethods.test_uninitialized_modules and test_module ModuleTests.test_uninitialized (module objects made without __init__); test_importlib extension MultiPhaseExtensionModuleTests.test_bad_modules; test_frame (inside traceback._compute_suggestion_error); test_symtable (weakref get at class body); test_free_threading.test_mmap (memoryview over mmap from threads); test_exceptions (SIGSEGV, case not located). test_descr and test_exceptions passed in the 2026-11-04 core-type run, so at least those are regressions. Located cases are in results/libtest/lucifer.excluded.tsv against this ticket.


## Notes

**2026-11-09T09:18:51Z**

Full run 2026-11-09 after the first exclusions: segfaults still zero test_pdb 152 (luc-gskg), test_exceptions 111, test_frame 53, test_syntax 48 (doctest), test_super 40, test_tuple 39, test_symtable 36, test_codeop 13, test_free_threading.test_mmap 10. test_super and test_tuple are new since the 2026-11-04 core-type run.
