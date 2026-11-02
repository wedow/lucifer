"""slots delete descriptor."""
from support import loops


def run():
    class C: __slots__ = ('x',)
    c = C(); C.x.__set__(c, 8); before = C.x.__get__(c, C); del c.x
    return before, hasattr(c, 'x')


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
