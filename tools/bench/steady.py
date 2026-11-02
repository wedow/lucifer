"""Identical suffix rule to the donor Smalltalk VM's all-runtimes harness.

Rule credited to tools/perf/all-runtimes/analyze.py at origin/master
(a71c8c05f32d7b01191e60a9ab8c773959613ef6), and the methodology in
vm/results/all-runtimes-2026-11-01/README.md. Reimplemented here in stdlib Python.
"""
import math
import statistics


def steady_start(samples):
    n = len(samples)
    rolling = [statistics.median(samples[i:i + 5]) for i in range(n - 4)]
    trends = [0] * n
    for i in range(n - 2, -1, -1):
        trends[i] = trends[i + 1] + sum((v > samples[i]) - (v < samples[i])
                                      for v in samples[i + 1:])
    for start in range(n - 11):
        tail = samples[start:]
        center = statistics.median(tail)
        mad = statistics.median(abs(v - center) for v in tail)
        band = max(center * 0.01, 3 * mad)
        if abs(tail[0] - center) > band or any(abs(v - center) > band for v in rolling[start:]):
            continue
        size = len(tail)
        limit = 1.96 * math.sqrt(size * (size - 1) * (2 * size + 5) / 18)
        shift = abs(statistics.median(tail[:size // 2]) - statistics.median(tail[size // 2:]))
        if abs(trends[start]) > limit and shift > center * 0.01:
            continue
        return start
    return None


def median_interval(samples):
    ordered = sorted(samples)
    size, tail, rank = len(samples), 0, 0
    for k in range(1, size // 2 + 1):
        tail += math.comb(size, k - 1)
        if 40 * tail > 2 ** size:
            break
        rank = k
    return (ordered[rank - 1], ordered[size - rank]) if rank else (None, None)


def summarize(samples, wall_seconds):
    start = steady_start(samples)
    retained = samples[start:] if start is not None else samples[len(samples) // 2:]
    median = statistics.median(retained)
    low, high = median_interval(retained)
    return dict(iterations=len(samples), steady='yes' if start is not None else 'fallback',
                steady_start=start + 1 if start is not None else None, steady_n=len(retained),
                median_us=median, ci_low_us=low, ci_high_us=high,
                first_iteration_us=samples[0], first_over_median=samples[0] / median,
                warmup_us=sum(samples[:start]) if start is not None else None,
                warmup_cost_us=sum(samples[:start]) - start * median if start is not None else None,
                startup_overhead_us=wall_seconds * 1e6 - sum(samples))
