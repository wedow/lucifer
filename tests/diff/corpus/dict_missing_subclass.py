"""dict missing subclass."""
from support import loops


def run():
    class D(dict):
        def __missing__(self, key): return ('missing', key)
    d = D(a=1)
    return d['x'], d.get('x'), 'x' in d, d.setdefault('y', 3), list(d.items())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
