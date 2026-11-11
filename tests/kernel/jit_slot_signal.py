# Signal handlers that walk the stack while compiled code reads __slots__
# members: a hot inlined `o.a` and getattr(o, 'a') on an unset slot inside
# try/except (its
# AttributeError is made by running Python), and optional reads of the same
# slot with getattr and hasattr. The handler sees whole frames each time.
import signal
import sys

ticks = []


def handler(signum, frame):
    names = []
    f = frame
    while f is not None:
        names.append(f.f_code.co_name)
        f = f.f_back
    inner = 'read' in names or 'optional' in names
    ticks.append(names[-1] == '<module>' and (not inner or 'run' in names))


class S:
    __slots__ = ('a', 'b')


def read(o):
    try:
        return o.a
    except AttributeError:
        pass
    try:
        return getattr(o, 'a')
    except AttributeError:
        return 'unset'


def optional(o):
    return getattr(o, 'a', 'missing'), hasattr(o, 'b')


def run(n):
    s, t = S(), S()
    t.a = 'set'
    out = set()
    for i in range(n):
        out.add(read(s))
        out.add(read(t))
        out.add(optional(s))
    return sorted(map(str, out))


signal.signal(signal.SIGALRM, handler)
signal.setitimer(signal.ITIMER_REAL, 0.001, 0.001)
try:
    result = run(200000)
finally:
    signal.setitimer(signal.ITIMER_REAL, 0, 0)
print(result)
print(len(ticks) > 0, all(ticks) if ticks else True)
