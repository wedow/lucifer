"""time (Modules/timemodule.c): clocks and sleep. sleep parks the task until
its deadline. Calendar conversion arrives with the structured time type."""

import _lucifer

CLOCK_REALTIME = 0
CLOCK_MONOTONIC = 1
CLOCK_PROCESS_CPUTIME_ID = 2
CLOCK_THREAD_CPUTIME_ID = 3
CLOCK_MONOTONIC_RAW = 4
CLOCK_BOOTTIME = 7
CLOCK_TAI = 11

_clock = _lucifer.os_clock_gettime


def clock_gettime_ns(clk_id, /):
    """Return the time of the specified clock clk_id as nanoseconds (int)."""
    return _clock(clk_id)


def clock_gettime(clk_id, /):
    """Return the time of the specified clock clk_id (float)."""
    return _clock(clk_id) / 1e9


def time_ns():
    """Return the current time in nanoseconds since the Epoch."""
    return _clock(CLOCK_REALTIME)


def time():
    """Return the current time in seconds since the Epoch."""
    return _clock(CLOCK_REALTIME) / 1e9


def monotonic_ns():
    """Monotonic clock, cannot go backward, as nanoseconds."""
    return _clock(CLOCK_MONOTONIC)


def monotonic():
    """Monotonic clock, cannot go backward."""
    return _clock(CLOCK_MONOTONIC) / 1e9


def perf_counter_ns():
    """Performance counter for benchmarking as nanoseconds."""
    return _clock(CLOCK_MONOTONIC)


def perf_counter():
    """Performance counter for benchmarking."""
    return _clock(CLOCK_MONOTONIC) / 1e9


def process_time_ns():
    """Process time for profiling as nanoseconds:
sum of the kernel and user-space CPU time."""
    return _clock(CLOCK_PROCESS_CPUTIME_ID)


def process_time():
    """Process time for profiling: sum of the kernel and user-space CPU time."""
    return _clock(CLOCK_PROCESS_CPUTIME_ID) / 1e9


def thread_time_ns():
    """Thread time for profiling as nanoseconds:
sum of the kernel and user-space CPU time."""
    return _clock(CLOCK_THREAD_CPUTIME_ID)


def thread_time():
    """Thread time for profiling: sum of the kernel and user-space CPU time."""
    return _clock(CLOCK_THREAD_CPUTIME_ID) / 1e9


def sleep(seconds, /):
    """Delay execution for a given number of seconds.  The argument may be
a floating-point number for subsecond precision."""
    if not isinstance(seconds, (int, float)):
        raise TypeError("'%s' object cannot be interpreted as an integer or float" % type(seconds).__name__)
    if seconds < 0:
        raise ValueError('sleep length must be non-negative')
    _lucifer.os_sleep(round(seconds * 1e9))
