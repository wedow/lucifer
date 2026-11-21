---
id: luc-9kal
status: closed
deps: []
links: []
created: 2026-11-06T21:49:51Z
type: task
priority: 2
assignee: Greg Wedow
---
# Kernel _args padding allocates on every call

From luc-jp30: (*args, MISSING) padding in the kernel's _args dominates str.startswith once inlined (alloc, free, locks in perf). Remove the allocation generally (scalar replacement or no padding tuple).


## Notes

**2026-11-21T13:19:00Z**

The positional parsers in `lib/kernel/str.py` and `lib/kernel/bytes.py` built a
temporary list and tuple for each missing argument. The methods now check arity
through a shared helper, read the arguments that were supplied, and apply
defaults directly. Dict construction and update share the arity error check.

The positional growth test failed on cd42a331 with 167,333 compiled tuple and
list allocations at 20,000 iterations and 327,333 at 40,000. The branch counted
6,577 at both sizes and matched the free-threaded CPython oracle. All 20 targeted
kernel checks passed in plain, jit and jit-deopt. The harness checks passed.

The user-space instruction counts are medians of five interleaved, pinned
launches per build on the test host with the default JIT and `PYTHONHASHSEED=0`.
Both builds used the same checkout and executable paths. The microbenchmark
runs 2,000,000 calls to `str.startswith` with a start bound after a 10,000-call
warmup. Each pyperformance row runs one workload iteration per process. Counts
include startup and warmup.

| Workload | Base instructions (millions) | Branch instructions (millions) | Change |
| --- | ---: | ---: | ---: |
| `str.startswith` with start | 4,893.360 | 1,770.313 | -63.82% |
| `docutils` | 85,864.875 | 85,750.309 | -0.13% |
| `pyflate` | 11,696.441 | 11,685.123 | -0.10% |
| `tomli_loads` | 36,004.152 | 35,598.131 | -1.13% |
