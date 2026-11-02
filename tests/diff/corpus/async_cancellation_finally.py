"""async cancellation finally."""
from support import loops


def run():
    import asyncio
    log = []
    async def child(ready):
        try:
            ready.set(); await asyncio.Future()
        finally: log.append('finally')
    async def task():
        ready = asyncio.Event(); t = asyncio.create_task(child(ready)); await ready.wait(); t.cancel()
        try: await t
        except asyncio.CancelledError: log.append('cancelled')
    asyncio.run(task())
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
