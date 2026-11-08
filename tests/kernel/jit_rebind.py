# luc-ffdo: functions that read a module global and rebind it on every
# call (a last-value cache, a counter). Each rebinding takes back what
# compiled code relied on about the global; the reader must settle on a
# unit that loads it generically, not recompile until the tier gives up.
# tools/jitcheck runs this at two sizes and requires that exits do not
# grow with the iterations and that no function here is disabled or
# retired.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

last = None
count = 0


def cached(x):
    global last
    if last is not None and last[0] == x:
        return last[1]
    r = x * 2
    last = (x, r)
    return r


def tick():
    global count
    count += 1
    return count


def run(n):
    s = 0
    for i in range(n):
        s += cached(i & 7) + tick()
    return s


print(run(N), count)
