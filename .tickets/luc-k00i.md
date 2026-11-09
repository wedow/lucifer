---
id: luc-k00i
status: open
deps: []
links: []
created: 2026-11-09T00:33:50Z
type: bug
priority: 2
tags: [stdlib, ssl]
---
# test_asyncio.test_ssl hangs; test_multiprocessing_main_handling times out

With _ssl and _multiprocessing built (luc-mods), test.test_asyncio.test_ssl times out as a whole module (all 24 cases TIMEOUT; CPython 3.14t passes 22) and test_multiprocessing_main_handling passes 17 of 39 (CPython 39), the rest timing out. test_asyncio.test_sslproto passes 25/27 and the corpus asyncio_tcp_ssl passes, so the hang is in something test_ssl's server threads do. Reproduce: bin/lucifer -m test -v test.test_asyncio.test_ssl -m test_create_connection_ssl_1.

