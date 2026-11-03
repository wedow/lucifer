---
id: luc-q6qr
status: open
deps: []
links: []
created: 2026-11-03T10:33:36Z
type: feature
priority: 0
parent: luc-66sy
tags: [kernel, stdlib]
---
# M0.0c: stdlib on sys.path with build-time precompiled pycs, and the import closure for os, unittest and test.support

sys.path gets the stdlib (third_party/cpython/Lib, precompiled by the host to build/lib pycs per DESIGN 3.3) and lib-dynload equivalent; port the native modules the closure needs (unicodedata over runtime/prim/unicodedata.c, _locale, _struct, math, _random, _bisect/_heapq if no Python twin, _pickle via pickle.py fallback, _functools if needed, select/fcntl minimal, _stat, _posixsubprocess later). Goal: tools/libtest can run Lib/test modules under bin/lucifer.

## Acceptance Criteria

import os, unittest, test.support, warnings, traceback, pickle, copy, unicodedata, locale, contextlib, difflib, pprint succeed; tools/libtest run --python bin/lucifer --modules test_int test_bool test_list test_tuple test_dict test_set test_str test_bytes produces real PASS/FAIL rows (not all NOTRUN) and the scores are committed under results/libtest/lucifer.tsv

