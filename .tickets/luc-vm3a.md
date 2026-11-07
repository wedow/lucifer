---
id: luc-vm3a
status: open
deps: []
links: []
created: 2026-11-07T21:12:05Z
type: epic
priority: 2
assignee: Greg Wedow
tags: [native, glm]
---
# native text primitives: pure C ports of Lemire's libraries

Owner 11-07: port fast_float, simdutf and simdjson to pure C (no C++ in the build) and adapt them to Python's exact semantics, so leaf text/number paths never need revisiting once the JIT exposes them. Method per library: port its upstream test suite first, then build the C implementation until 100% pass, then differential/fuzz tests against CPython 3.14t (results, exceptions, messages, positions). Keep upstream license notices (MIT/Apache-2.0) in the ported sources. Runtime dispatch on CPU features (SSE4.2/AVX2/AVX-512, NEON later) with a scalar path that passes the same tests. Native code exposes primitives; Python-visible semantics (hooks, error handlers, object building where the JIT benefits) stay in lib/ where that is simpler. Each primitive gets an effect annotation the JIT can use (pure, no escape, cannot run Python) so compiled code can CSE/hoist around it.

