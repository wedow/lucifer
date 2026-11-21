---
id: luc-6s0a
status: closed
deps: []
links: []
created: 2026-11-21T09:28:19Z
type: task
priority: 2
---
# perf: 11-21 daily regressions bisected

Confirm the November 21 daily benchmark regressions with retired instruction counts and identify the first bad changes.


## Notes

**2026-11-21T10:56:04Z**

All eight rows fell below the roughly 5% confirmation threshold in retired instructions and were dropped from the bisect. Their wall-time causes remain unverified.

| Benchmark | Daily wall time Δ | Base median | Candidate median | Retired instructions Δ | First bad commit | Cause |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| `hexiom` | +26.2% | 3,199,592,818 | 3,228,517,292 | +0.90% | — | Unverified |
| `sqlite_synth` | +22.9% | 83,282,257 | 81,054,024 | -2.68% | — | Unverified |
| `regex_v8` | +22.7% | 14,231,020,799 | 14,217,800,365 | -0.09% | — | Unverified |
| `argparse_subparsers` | +12.6% | 11,864,572,191 | 11,873,627,979 | +0.08% | — | Unverified |
| `deltablue` | +12.0% | 2,929,159,284 | 2,884,747,529 | -1.52% | — | Unverified |
| `telco` | +11.6% | 7,280,613,419 | 7,117,029,431 | -2.25% | — | Unverified |
| AWFY Havlak | +10.4% | 62,602,100,227 | 62,518,403,555 | -0.13% | — | Unverified |
| AWFY CD | +6.9% | 34,139,217,228 | 34,203,072,572 | +0.19% | — | Unverified |

Compared `62fd89eb` and `3c94548c` with default JIT and `PYTHONHASHSEED=0`. Built each revision at the same checkout path and restored its binary and standard library there for each trial. Each row used five fresh processes per revision, alternating the arm order and pinning each process to one core on the test host. Deltas compare the medians of userspace retired instructions.

Used pyperformance 1.14.0 and pyperf 2.10.0 with the daily's CPython 3.14t loop counts, one warm-up and three measured values. For AWFY, counted all three correctness-checked iterations at the daily's inner sizes of 1500 for Havlak and 250 for CD. Perf counters were active around the benchmark work, outside startup, imports and pyperf JSON output. [Benchmark definitions](../tools/bench/corpus), [reference loop counts](../results/status-reference.json), [November 21 daily results](../results/status-2026-11-21/summary.csv).
