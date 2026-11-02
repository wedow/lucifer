"""async generator iteration."""
from support import loops


def run():
    import asyncio
    async def gen():
        for x in range(4): yield x * x
    async def task(): return [x async for x in gen() if x % 2 == 0]
    return asyncio.run(task())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
