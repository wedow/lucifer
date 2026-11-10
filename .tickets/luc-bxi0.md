---
id: luc-bxi0
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: bug
priority: 2
tags: [libtest]
---
# libtest: multiprocessing forkserver start method unavailable (399 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. multiprocessing reports 'forkserver start method not available', so test_multiprocessing_forkserver (test_processes 163, test_misc 83, test_threads 79, test_manager 69) skips, plus 5 forkserver cases in the spawn and fork misc modules. CPython checks reduction.HAVE_SEND_HANDLE (socket fd passing over AF_UNIX); unverified which piece Lucifer lacks.

