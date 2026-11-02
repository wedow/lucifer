"""weakref weakmethod."""
from support import loops


def run():
    import gc, weakref
    class C:
        def f(self): return 9
    c = C(); c.self = c; method = weakref.WeakMethod(c.f); before = method()()
    del c; gc.collect(); gc.collect()
    return before, method() is None


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
