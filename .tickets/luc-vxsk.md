---
id: luc-vxsk
status: in_progress
deps: []
links: []
created: 2026-11-09T00:14:12Z
type: epic
priority: 1
tags: [perf, jit, web, asyncio]
---
# web and async: Flask and the async_tree family, profile then general fixes

Flask (werkzeug + Jinja2) runs about 5x CPython 3.14t's time per request in process; the 16 pyperformance async_tree variants (29.5% of the corpus's log-pull) run 5-12x. Profile both on master by mechanism (compiled vs interpreted Python, runtime C, event loop machinery, coroutine completion, context vars, Jinja template code, werkzeug request/response objects), then fix the biggest mechanisms in general form. Candidate tickets: luc-j7zk, luc-s4lm, luc-mltz, luc-6ljp, luc-f7cn.

