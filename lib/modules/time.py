"""time (Modules/timemodule.c): clocks, sleep and calendar conversion. sleep
parks the task until its deadline; the calendar functions run over the C
library through the primitives of runtime/prim/calendar.c."""

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


def clock_getres(clk_id, /):
    """Return the resolution (precision) of the specified clock clk_id."""
    return _lucifer.clock_getres(clk_id) / 1e9


def get_clock_info(name, /):
    """Get information of the specified clock."""
    clocks = {
        'monotonic': ('clock_gettime(CLOCK_MONOTONIC)', CLOCK_MONOTONIC, True, False),
        'perf_counter': ('clock_gettime(CLOCK_MONOTONIC)', CLOCK_MONOTONIC, True, False),
        'process_time': ('clock_gettime(CLOCK_PROCESS_CPUTIME_ID)', CLOCK_PROCESS_CPUTIME_ID, True, False),
        'thread_time': ('clock_gettime(CLOCK_THREAD_CPUTIME_ID)', CLOCK_THREAD_CPUTIME_ID, True, False),
        'time': ('clock_gettime(CLOCK_REALTIME)', CLOCK_REALTIME, False, True),
    }
    if name not in clocks:
        raise ValueError('unknown clock')
    implementation, clock, monotonic, adjustable = clocks[name]
    import types
    return types.SimpleNamespace(implementation=implementation, monotonic=monotonic,
                                 adjustable=adjustable, resolution=clock_getres(clock))


# Calendar time

struct_time = _lucifer.structseq.structseq_type(
    'time.struct_time', 'The time value as returned by gmtime(), localtime(), and strptime(), and\n'
    ' accepted by asctime(), mktime() and strftime().  May be considered as a\n'
    ' sequence of 9 integers.\n\n'
    ' Note that several fields\' values are not the same as those defined by\n'
    ' the C language standard for struct tm.  For example, the value of the\n'
    ' field tm_year is the actual year, not year - 1900.  See individual\n'
    ' fields\' descriptions for details.',
    ('tm_year', 'tm_mon', 'tm_mday', 'tm_hour', 'tm_min', 'tm_sec', 'tm_wday', 'tm_yday',
     'tm_isdst', 'tm_zone', 'tm_gmtoff'), 9)
_make_struct_time = _lucifer.structseq.structseq_make
_DAYS = ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')
_MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')
_INT_MAX = 2147483647


def _time_t(seconds):
    """_PyTime_ObjectToTime_t with rounding toward minus infinity."""
    if seconds is None:
        return _clock(CLOCK_REALTIME) // 1000000000
    if isinstance(seconds, float):
        if seconds != seconds:
            raise ValueError('Invalid value NaN (not a number)')
        if not -9.223372036854776e18 <= seconds < 9.223372036854776e18:
            raise OverflowError('timestamp out of range for platform time_t')
        return int(seconds // 1.0)
    try:
        seconds = seconds.__index__()
    except AttributeError:
        raise TypeError("'%s' object cannot be interpreted as an integer"
                        % type(seconds).__name__) from None
    if not -(1 << 63) <= seconds < (1 << 63):
        raise OverflowError('timestamp out of range for platform time_t')
    return seconds


def _struct_time(seconds, local):
    fields = _lucifer.tm_tuple(_time_t(seconds), local)
    if not -_INT_MAX - 1 + 1900 <= fields[0] <= _INT_MAX + 1900:
        raise ValueError('year out of range')
    return _make_struct_time(struct_time, fields[:9], {'tm_zone': fields[9], 'tm_gmtoff': fields[10]})


def gmtime(seconds=None, /):
    """Convert seconds since the Epoch to a time tuple expressing UTC (a.k.a. GMT)."""
    return _struct_time(seconds, False)


def localtime(seconds=None, /):
    """Convert seconds since the Epoch to a time tuple expressing local time."""
    return _struct_time(seconds, True)


def _tm_arg(t):
    """gettmarg: nine integers, from a struct_time or a tuple."""
    if not isinstance(t, tuple):
        raise TypeError('Tuple or struct_time argument required')
    if not isinstance(t, struct_time):
        t = struct_time(t)
    fields = tuple(t)
    for value in fields:
        if not isinstance(value, int):
            raise TypeError("'%s' object cannot be interpreted as an integer" % type(value).__name__)
    return t, fields


def _check_tm(fields, strict_year=False):
    """checktm with the conversions gettmarg and checktm apply: the fields
    the C library takes (month 1-12, day of year 1-366 as in struct_time)."""
    year, mon, mday, hour, minute, sec, wday, yday, isdst = fields
    if mon == 0:
        mon = 1
    elif not 1 <= mon <= 12:
        raise ValueError('month out of range')
    if mday == 0:
        mday = 1
    elif not 0 <= mday <= 31:
        raise ValueError('day of month out of range')
    if not 0 <= hour <= 23:
        raise ValueError('hour out of range')
    if not 0 <= minute <= 59:
        raise ValueError('minute out of range')
    if not 0 <= sec <= 61:
        raise ValueError('seconds out of range')
    wday = (wday + 1) % 7
    if wday < 0:
        raise ValueError('day of week out of range')
    if yday == 0:
        yday = 1
    elif not 1 <= yday <= 366:
        raise ValueError('day of year out of range')
    return (year, mon, mday, hour, minute, sec, (wday + 6) % 7, yday, isdst)


def asctime(time_tuple=None, /):
    """Convert a time tuple to a string, e.g. 'Sat Jun 06 16:26:11 1998'."""
    t = localtime() if time_tuple is None else _tm_arg(time_tuple)[0]
    year, mon, mday, hour, minute, sec, wday, yday, isdst = _check_tm(tuple(t)[:9])
    return '%s %s%3d %02d:%02d:%02d %d' % (_DAYS[wday], _MONTHS[mon - 1], mday, hour, minute, sec, year)


def ctime(seconds=None, /):
    """Convert a time in seconds since the Epoch to a string in local time."""
    try:
        t = localtime(seconds)
    except ValueError:
        raise ValueError('unconvertible time') from None
    return asctime(t)


def mktime(time_tuple, /):
    """Convert a time tuple in local time to seconds since the Epoch."""
    t, fields = _tm_arg(time_tuple)
    seconds = _lucifer.mktime(fields)
    if seconds is None:
        raise OverflowError('mktime argument out of range')
    return float(seconds)


def strftime(format, time_tuple=None, /):
    """Convert a time tuple to a string according to a format specification."""
    if not isinstance(format, str):
        raise TypeError('strftime() argument 1 must be str, not %s' % type(format).__name__)
    if time_tuple is None:
        t = localtime()
    else:
        t = _tm_arg(time_tuple)[0]
    fields = _check_tm(tuple(t)[:9])
    if '\0' in format:
        raise ValueError('embedded null character')
    zone = gmtoff = None
    if isinstance(t, struct_time):
        zone, gmtoff = t.tm_zone, t.tm_gmtoff
    return _lucifer.strftime(format, fields, zone, gmtoff or 0)


def strptime(data_string, format='%a %b %d %H:%M:%S %Y', /):
    """Parse a string to a time tuple according to a format specification."""
    import _strptime
    return _strptime._strptime_time(data_string, format)


def _init_timezone():
    global timezone, altzone, daylight, tzname
    year = 365.24 * 24 * 3600
    now = (_clock(CLOCK_REALTIME) // 1000000000) // int(year) * int(year)
    jan = _lucifer.tm_tuple(now, True)
    july = _lucifer.tm_tuple(now + int(year / 2), True)
    janzone, janname = -jan[10], jan[9]
    julyzone, julyname = -july[10], july[9]
    if janzone < julyzone:
        timezone, altzone = julyzone, janzone
        daylight = int(janzone != julyzone)
        tzname = (julyname, janname)
    else:
        timezone, altzone = janzone, julyzone
        daylight = int(janzone != julyzone)
        tzname = (janname, julyname)


def tzset():
    """Initialize, or reinitialize, the local timezone to the value stored in os.environ['TZ']."""
    _lucifer.tzset()
    _init_timezone()


_lucifer.tzset()
_init_timezone()
