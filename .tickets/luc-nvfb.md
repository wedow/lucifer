---
id: luc-nvfb
status: closed
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

**2026-11-10T03:44:42Z**

Fixed on luc-crash, each with a kernel or C-API test: (1) ModuleType.__new__ without __init__ left md_dict NULL, which the LOAD_ATTR specializer and the module fast paths read (test_module, test_descr uninitialized cases); the module now gets its dict at creation. Present since the 11-03 inline caches, not a regression since 11-04. (2) _IncompleteInputError was skipped by the exception setup (exported as the type itself), so its instances had no dealloc: codeop, code.interact, pdb crashed (test_codeop, test_code_module, test_pdb; luc-gskg). (3) Native tuple hash recursed without a stack check since 43d1e5b (luc-pfnu): test_tuple test_hash_deeply_nested. (4) A returning frame stayed current while its locals were released, so a finalizer made a frame object of it that dangled (GH-99729; test_frame test_sneaky_frame_object_teardown); interpreter and compiled returns now unlink first. Pre-existing since 11-04. (5) PySTEntry_Type was never readied, so hashing a symbol table entry read a NULL dict (test_symtable). (6) Concurrent __class__ assignment to one object released the old class twice (test_super multithreaded case; on 11-04 the same race hung instead): now per-object critical section. (7) An extension init returning an uninitialized PyModuleDef was dereferenced (test_importlib test_bad_modules); now SystemError, unreported exceptions chained. (8) PyErr_GivenExceptionMatches recursed without a bound (test_exceptions gh-156204 case; exposed once _testcapi loaded, the 11-04 build could not load it). (9) PEP 688 __release_buffer__ was never called, and mmap tracked no exports, so close() unmapped memory a live memoryview read (test_free_threading.test_mmap). All 13 excluded cases now run: none crashes; 9 pass, test_code_module test_syntax_error and test_sysexcepthook_syntax_error FAIL (luc-hjnt is a related divergence), test_inspect test_getfullargspec_builtin_func_no_signature and test_signature_on_builtins_no_signature ERROR.

**2026-11-10T08:00:08Z**

Also fixed: luc-hjnt (SyntaxError text word, from e1c454f) brings test_code_module to 17 of 18.
