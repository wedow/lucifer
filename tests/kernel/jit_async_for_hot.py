# A hot `async for` over an async generator and over a class with
# __anext__, and a hot `async with`: the frames that run them are
# compiled, with no side exits and no entries left to the interpreter per
# iteration.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 1000


async def agen(n):
    for i in range(n):
        yield i


class Counter:
    def __init__(self, n):
        self.i, self.n = 0, n

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.i >= self.n:
            raise StopAsyncIteration
        self.i += 1
        return self.i


class Manager:
    async def __aenter__(self):
        return 1

    async def __aexit__(self, typ, val, tb):
        return False


async def body(i):
    total = 0
    async for v in agen(3):
        total += v
    async for v in Counter(2):
        total += v
    async with Manager() as m:
        total += m + i
    return total


def run(n):
    total = 0
    for i in range(n):
        c = body(i)
        try:
            c.send(None)
        except StopIteration as e:
            total += e.value
    return total


print(run(N))
