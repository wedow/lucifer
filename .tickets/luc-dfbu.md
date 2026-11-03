---
id: luc-dfbu
status: closed
deps: []
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 0
assignee: Greg Wedow
parent: luc-nsmh
tags: [runtime, abi, slice]
---
# runtime: canonical cp314t headers, exported ABI surface and static objects

Makefile, include/ (verbatim v3.14.10 Include/ without internal/, cp314t pyconfig.h), runtime/types/static.c with all 215 exported data objects, every exported function defined with its header prototype (unported ones abort naming themselves), mimalloc, biased refcount, tools/abi and results/abi/cp314t-exports.tsv (docs/DESIGN.md 4.1, 4.2, 9.2).

## Acceptance Criteria

make abi-audit: bin/lucifer exports all 1,843 Py*/_Py* symbols of the cp314t oracle and all 215 data symbols have the oracle's nm -S sizes; the audit prints implemented vs stub counts.


## Notes

**2026-11-03T02:02:34Z**

Merged 9cde773. 1843/1843 exports, 215/215 data sizes, 24 implemented, 1604 weak stubs. dlopen check via LD_BIND_NOW.
