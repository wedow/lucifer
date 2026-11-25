# A hot `yield from` and a hot `await`: delegating frames made and run to
# their end each iteration (a generator delegating to a generator and to a
# list iterator, a coroutine awaiting coroutines that return at once, an
# await chain that suspends once and is resumed). Their SEND runs in
# compiled code: no exits and no entries left to the interpreter per
# iteration.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000


def inner(i):
    yield i
    yield i + 1
    return i * 2


def outer(i):
    r = yield from inner(i)
    yield from [r, r + 1]
    return r


async def value(i):
    return i


async def adds(i):
    return await value(i) + await value(1)


class Step:
    def __await__(self):
        got = yield 'step'
        return got


async def suspends(depth):
    if depth == 0:
        return await Step()
    return await suspends(depth - 1) + 1


def run(n):
    total = 0
    for i in range(n):
        for v in outer(i):
            total += v
        c = adds(i)
        try:
            c.send(None)
        except StopIteration as e:
            total += e.value
        c = suspends(3)
        c.send(None)
        try:
            c.send(i)
        except StopIteration as e:
            total += e.value
    return total


print(run(N))
