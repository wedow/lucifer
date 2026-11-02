"""weakref explicit collection."""
from support import loops


def run():
    import gc, weakref
    log = []
    class C: pass
    c = C(); c.self = c; ref = weakref.ref(c, lambda r: log.append(r() is None))
    before = ref() is c; del c; gc.collect(); gc.collect()
    return before, ref() is None, log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
