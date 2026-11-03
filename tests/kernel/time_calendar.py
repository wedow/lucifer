# time's calendar functions, in fixed zones.
import os
import time

for tz in ('UTC', 'America/New_York', 'Asia/Kolkata', 'Australia/Sydney'):
    os.environ['TZ'] = tz
    time.tzset()
    print(tz, time.timezone, time.altzone, time.daylight, time.tzname)
    for secs in (0, 86400 * 200 + 3600, 1700000000, -1, -86400 * 365 * 50, 4102444800.7, 2**40):
        g, l = time.gmtime(secs), time.localtime(secs)
        print(secs, tuple(g), g.tm_zone, g.tm_gmtoff, tuple(l), l.tm_zone, l.tm_gmtoff)
        print(time.asctime(l), '|', time.ctime(secs), '|', time.mktime(l), time.strftime('%Y-%m-%d %H:%M:%S %a %b %j %Z %z %%', l))
os.environ['TZ'] = 'UTC'
time.tzset()
t = time.gmtime(1234567890)
print(t, t.n_fields, t.n_sequence_fields, t[0], t[:3], time.struct_time((2000, 1, 2, 3, 4, 5, 6, 7, 0)))
print(time.strftime('%A %B %d %y %I %p %U %w', t), repr(time.strftime('')), time.strftime('%c|%x|%X', t))
print(time.strftime('%Y', (1999, 0, 0, 0, 0, 0, 0, 0, -1)))
for bad in [lambda: time.gmtime('x'), lambda: time.gmtime(float('nan')), lambda: time.gmtime(1e300), lambda: time.gmtime(2**63),
            lambda: time.gmtime(-2**70), lambda: time.asctime((2000, 13, 1, 0, 0, 0, 0, 1, 0)),
            lambda: time.asctime((2000, 1, 1, 24, 0, 0, 0, 1, 0)), lambda: time.strftime('%Y', (2000, 1, 1, 0, 60, 0, 0, 1, 0)),
            lambda: time.strftime('%Y', (2000, 1, 1, 0, 0, 0, 0, 400, 0)), lambda: time.strftime(5),
            lambda: time.strftime('a\0b'), lambda: time.mktime(5), lambda: time.mktime((1,) * 8),
            lambda: time.asctime('x'), lambda: time.mktime((2**40, 1, 1, 0, 0, 0, 0, 1, 0)), lambda: time.gmtime(2**60)]:
    try:
        print(bad())
    except Exception as e:
        print(type(e).__name__, e)
print(time.gmtime(1.9)[5], time.gmtime(-1.5)[5], time.ctime(0) == time.asctime(time.localtime(0)))
print(sorted(vars(time.get_clock_info('time'))), time.get_clock_info('monotonic').implementation, time.clock_getres(time.CLOCK_MONOTONIC) > 0)
try:
    time.get_clock_info('x')
except ValueError as e:
    print(e)
print(time.strptime('2001-02-03 04:05:06', '%Y-%m-%d %H:%M:%S'))
