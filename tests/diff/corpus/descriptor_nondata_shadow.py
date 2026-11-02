"""descriptor nondata shadow."""
from support import loops


def run():
    class D:
        def __get__(self, obj, cls): return 11
    class C: x = D()
    c = C(); before = c.x; c.x = 99; after = c.x; del c.x
    return before, after, c.x


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
