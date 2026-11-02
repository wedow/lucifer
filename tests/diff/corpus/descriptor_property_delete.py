"""descriptor property delete."""
from support import loops


def run():
    log = []
    class C:
        def __init__(self): self._x = 1
        @property
        def x(self): return self._x
        @x.setter
        def x(self, value): log.append(('set', value)); self._x = value
        @x.deleter
        def x(self): log.append(('del', self._x)); del self._x
    c = C(); c.x = 7; value = c.x; del c.x
    return value, log, hasattr(c, 'x')


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
