# A hot await of coroutines that return at once, and a hot `yield from`
# of a generator that is started and run to its end inside it: each
# coroutine or generator starts in its call's compiled code, with no turn
# of the interpreter, whether the interpreter, send() or a compiled
# delegation resumes it.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000


async def value(i):
    return i


async def adds(i):
    return await value(i) + await value(1)


def inner(i):
    return i * 2
    yield


def outer(i):
    r = yield from inner(i)
    return r


def run(n):
    total = 0
    for i in range(n):
        c = adds(i)
        try:
            c.send(None)
        except StopIteration as e:
            total += e.value
        g = outer(i)
        try:
            next(g)
        except StopIteration as e:
            total += e.value
    return total


print(run(N))
