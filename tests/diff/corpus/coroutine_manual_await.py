"""coroutine manual await."""
from support import loops


def run():
    class Await:
        def __await__(self):
            value = yield 'pause'
            return value + 1
    async def task(): return await Await()
    c = task(); it = c.__await__(); out = [next(it)]
    try: it.send(8)
    except StopIteration as e: out.append(e.value)
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
