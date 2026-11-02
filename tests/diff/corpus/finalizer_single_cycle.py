"""finalizer single cycle."""
from support import loops


def run():
    import gc
    log = []
    class C:
        def __del__(self): log.append('finalized')
    c = C(); c.self = c; del c; gc.collect(); gc.collect()
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
