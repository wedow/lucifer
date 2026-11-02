"""async contextvars."""
from support import loops


def run():
    import asyncio, contextvars
    v = contextvars.ContextVar('v', default=0)
    async def child(n):
        token = v.set(n); await asyncio.sleep(0); result = v.get(); v.reset(token); return result
    async def task(): return await asyncio.gather(child(1), child(2)), v.get()
    return asyncio.run(task())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
