---
id: luc-w4oh
status: open
deps: []
links: []
created: 2026-11-07T18:46:10Z
type: bug
priority: 2
tags: [bench, corpus]
---
# stdlib: _ssl is missing; four corpus programs fail

import ssl raises ModuleNotFoundError: No module named '_ssl'. Corpus programs (tools/bench/corpus) that fail on it: pyperformance asyncio_tcp and asyncio_tcp_ssl (asyncio imports ssl), tornado_http, fastapi (httpx imports ssl). Reproduce: PYTHONPATH=build/corpus/site bin/lucifer build/corpus/site/pyperformance/data-files/benchmarks/bm_asyncio_tcp/run_benchmark.py --worker -l 1 -w 0 -n 1.

