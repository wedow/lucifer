"""finalizer resurrection."""
from support import loops


def run():
    import gc
    survivors = []; log = []
    class C:
        def __del__(self): log.append('finalized'); survivors.append(self)
    c = C(); c.self = c; del c; gc.collect(); gc.collect()
    before = (list(log), len(survivors)); survivors.clear(); gc.collect(); gc.collect()
    return before, log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
