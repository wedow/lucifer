---
id: luc-lmao
status: open
deps: []
links: []
created: 2026-11-09T05:35:26Z
type: bug
priority: 1
---
# libtest: Lib/test cases that hang Lucifer's worker

Cases that hang until regrtest's timeout in the full Lib/test run (2026-11-09), each zeroing its module. Located so far: test_call TestPEP590.test_vectorcall (it hangs once the C-API aborts before it are excluded) (excluded in results/libtest/lucifer.excluded.tsv against this ticket). Module-level timeouts are listed in the triage notes.


## Notes

**2026-11-09T09:18:51Z**

Full Lib/test run 2026-11-09 (module timeout = max(180 s, 10x CPython's duration)): 2460 cases lost to module timeouts. Hanging case per module: test_io 673 (test_reentrant_write_text), test_socket 509 (testSendtoAndRecv), test_logging 276 (test_output), test_warnings 201 (test_fork_other_thread_holds_lock), test_multiprocessing_spawn.test_processes 157 (resource_tracker _stop_locked), test_itertools 133 (test_tee_concurrent), test_collections 116 (test_large_size), test_math 88 (testComb), test_hashlib 80 (test_blake2_update_over_4gb), test_unicodedata 60 (test_function_checksum), test_cmd_line_script 49 (test_repl_stderr_flush), test_codeccallbacks 44 (not located), test_free_threading.test_itertools 8, test_free_threading.test_weakref 4 (test_getweakref_no_crash). test_asyncio.test_ssl 22 and test_multiprocessing_main_handling 39 are luc-k00i. testComb, test_function_checksum, test_large_size and the 4 GB blake2 case are likely slowness rather than hangs (unverified).
