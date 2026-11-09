---
id: luc-w4oh
status: closed
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _ssl is missing; four corpus programs fail

import ssl raises ModuleNotFoundError: No module named '_ssl'. Corpus programs (tools/bench/corpus) that fail on it: pyperformance asyncio_tcp and asyncio_tcp_ssl (asyncio imports ssl), tornado_http, fastapi (httpx imports ssl). Reproduce: PYTHONPATH=build/corpus/site bin/lucifer build/corpus/site/pyperformance/data-files/benchmarks/bm_asyncio_tcp/run_benchmark.py --worker -l 1 -w 0 -n 1.


## Notes

**2026-11-09T00:33:44Z**

Fixed on luc-mods: _ssl is the tag's Modules/_ssl.c built into lib-dynload against the system OpenSSL; _socket.socket keeps its state in PySocketSockObject's layout (a C base type), which _ssl reads. asyncio_tcp, asyncio_tcp_ssl, tornado_http and fastapi pass the corpus. test_ssl 190/206 (CPython 3.14t 191). test_asyncio.test_ssl still times out as a module (luc-k00i).
