# Hot generators resumed every way a program resumes one: a for loop, next()
# and send(), a generator expression a builtin consumes, a `yield from`
# chain and a coroutine awaiting coroutines. Once compiled, a resumption
# enters the generator's compiled code: the interpreted resumes do not grow
# with the iterations (tools/jitcheck lane gens).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000


def numbers(n):
    i = 0
    while i < n:
        yield i
        i += 1


def echo():
    total = 0
    while True:
        got = yield total
        total += got


def leaf(n):
    for i in range(n):
        yield i * 2
    return n


def chain(n):
    r = yield from leaf(n)
    yield r


async def square(x):
    return x * x


async def total(n):
    s = 0
    for i in range(n):
        s += await square(i)
    return s


def run(n):
    a = 0
    for v in numbers(n):
        a += v
    g = echo()
    next(g)
    for i in range(n):
        a += g.send(i) & 7
    a += sum(x & 3 for x in range(n))
    for v in chain(n):
        a += v
    c = total(n)
    try:
        c.send(None)
    except StopIteration as e:
        a += e.value & 0xff
    return a


print(run(N))
