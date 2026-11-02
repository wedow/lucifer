"""descriptor getattr fallback."""
from support import loops


def run():
    class C:
        @property
        def x(self): raise AttributeError('inside getter')
        def __getattr__(self, name): return ('fallback', name)
    c = C()
    return c.x, c.unknown, hasattr(c, 'missing')


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
