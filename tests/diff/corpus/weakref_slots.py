"""weakref slots."""
from support import loops


def run():
    import weakref
    class C: __slots__ = ('x', '__weakref__')
    c = C(); c.x = 7; r = weakref.ref(c)
    return r() is c, r().x


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
