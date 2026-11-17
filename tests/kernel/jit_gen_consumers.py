# Hot code hands generators to the builtins that consume them (sum, list,
# tuple, set, dict, any, all, str.join, sorted, min, max) and steps
# generators it did not make with next() and send(). The builtins are kernel
# code that takes each item with next(), which resumes the generator: compiled
# code resumes it without leaving for the interpreter (tools/jitcheck lane
# consumers counts side exits). A generator that raises, returns a value, or
# is closed half-way behaves as CPython's (the first iterations only: those
# raise and throw, which leave compiled code).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000


def numbers(n):
    for i in range(n):
        yield i


def echo():
    total = 0
    while True:
        got = yield total
        total += got


def boom(n):
    yield n
    raise ValueError(n)


def returns(n):
    yield n
    return n * 2


def modes(mode):
    return sum(c in 'rwax' for c in mode)


def consume(i):
    a = modes('rb') + modes('w+')
    a += sum(numbers(4)) + len(list(x for x in numbers(3))) + len(tuple(numbers(2)))
    a += len(set(x & 1 for x in range(4))) + len(dict((x, x) for x in numbers(3)))
    a += any(x == 2 for x in numbers(4)) + all(x < 9 for x in numbers(3))
    a += len(''.join(c for c in 'abc')) + max(numbers(3)) + min(x for x in numbers(4)) + sorted(numbers(3))[0]
    return a


def steps(g, e):
    a = next(g) + next(g, 0)
    a += e.send(a & 7)
    return a


def edges(i):
    a = 0
    try:
        sum(boom(i))
    except ValueError as ex:
        a += ex.args[0] & 1
    g = returns(i)
    next(g)
    try:
        next(g)
    except StopIteration as ex:
        a += ex.value & 3
    a += next(g, 5)
    h = numbers(3)
    next(h)
    h.close()
    a += next(h, 7)
    return a


def run(n):
    a = 0
    e = echo()
    next(e)
    for i in range(n):
        a += consume(i)
        a += steps(numbers(3), e) & 0xff
        if i < 200:
            a += edges(i)      # raising and closing leave compiled code: only the first iterations
    return a


print(run(N))
