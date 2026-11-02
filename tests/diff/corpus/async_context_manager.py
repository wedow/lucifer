"""async context manager."""
from support import loops


def run():
    import asyncio
    log = []
    class Context:
        async def __aenter__(self): log.append('enter'); return 3
        async def __aexit__(self, typ, value, tb): log.append(('exit', typ.__name__)); return True
    async def task():
        async with Context() as x:
            log.append(x); raise ValueError('v')
        log.append('after')
    asyncio.run(task())
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
