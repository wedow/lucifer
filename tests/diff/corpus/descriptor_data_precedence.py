"""descriptor data precedence."""
from support import loops


def run():
    class D:
        def __get__(self, obj, cls): return 10 if obj else 'class'
        def __set__(self, obj, value): obj.__dict__['stored'] = value
    class C: x = D()
    c = C(); c.__dict__['x'] = 99; c.x = 7
    return c.x, C.x, c.__dict__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
