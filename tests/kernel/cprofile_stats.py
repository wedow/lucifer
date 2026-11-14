# cProfile over sys.monitoring: Python and C calls, recursion, exceptions and
# generators are counted as CPython counts them. A counting timer makes the
# times deterministic, so the whole pstats report is compared.
import cProfile
import io
import pstats

ticks = 0


def timer():
    global ticks
    ticks += 1
    return ticks


def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)


def gen(n):
    for i in range(n):
        yield i * i


def fails():
    raise ValueError("x")


def work():
    out = []
    for i in range(5):
        out.append(len(str(i)))
    fib(6)
    sum(gen(4))
    sorted([3, 1, 2], key=abs)
    try:
        fails()
    except ValueError:
        pass
    try:
        int("x")
    except ValueError:
        pass
    return out


p = cProfile.Profile(timer)
p.enable()
work()
p.disable()
s = io.StringIO()
pstats.Stats(p, stream=s).strip_dirs().sort_stats("calls", "name").print_stats()
pstats.Stats(p, stream=s).strip_dirs().sort_stats("name").print_callers("fib|len")
print(s.getvalue())

# runctx, the context manager and getstats() entries.
with cProfile.Profile(timer) as q:
    fib(3)
for e in sorted(q.getstats(), key=lambda e: str(e.code)):
    print(getattr(e.code, "co_name", e.code), e.callcount, e.reccallcount,
          sorted((getattr(c.code, "co_name", c.code), c.callcount) for c in e.calls or ()))
cProfile.Profile(timer).runctx("fib(4)", globals(), {}).print_stats("name")
