# Lucifer

A CPython-compatible Python with a JIT and true multicore parallelism, built on lessons from a Smalltalk VM with template and optimizing tiers, deoptimization, type feedback, flat object layout and a many-core scheduler.

The bar:
- numpy's own test suite passes against an unmodified numpy build, with the full C-API (non-limited) from day one.
- No GIL. Production Python behaves correctly, not just toy programs.
- The standard library, built-in types included, is mostly Python compiled by the JIT over a small set of native primitives.
- Every speed claim is measured against CPython, PyPy and free-threaded CPython, across a broad corpus, never one benchmark.

Status: interpreter running. 470+ kernel programs, 153/164 differential programs and the first CPython `Lib/test` modules match CPython 3.14t; the first unmodified C extensions are next. See `tools/scoreboard`.

License: MIT (see LICENSE). Vendored third-party code under `third_party/` keeps its own license.
