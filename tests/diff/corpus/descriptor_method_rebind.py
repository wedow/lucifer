"""descriptor method rebind."""
from support import loops


def run():
    class C:
        def f(self): return 1
    c = C(); saved = c.f
    for _ in range(loops()): c.f()
    C.f = lambda self: 2
    return saved(), c.f(), saved.__self__ is c


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
