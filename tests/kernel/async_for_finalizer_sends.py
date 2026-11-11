# Finalizers that step async generators of their own while an async for is
# between its send and its test of what came back: the collections that run
# them are made often, by cyclic garbage, so some land at that point when the
# loop is compiled. Each loop must still see its own items, in order.
import gc


class Request:
    def __await__(self):
        reply = yield 'request'
        return reply


async def items(n):
    for i in range(n):
        if i % 3 == 0:
            await Request()
        yield i


async def consume(n):
    out = []
    async for v in items(n):
        out.append(v)
    return out


def run(coro):
    steps = 0
    try:
        while True:
            assert coro.send(None) == 'request'
            steps += 1
    except StopIteration as e:
        return steps, e.value


class Stepper:
    """Garbage in a cycle whose finalizer leaves an async for suspended on a request."""

    def __init__(self):
        self.me = self

    def __del__(self):
        c = consume(4)
        c.send(None)
        c.close()


gc.set_threshold(50)
total = 0
for k in range(400):
    for _ in range(5):
        Stepper()
    steps, out = run(consume(10))
    if out != list(range(10)) or steps != 4:
        print('wrong', k, steps, out)
        break
    total += len(out)
gc.collect()
print('items', total)
