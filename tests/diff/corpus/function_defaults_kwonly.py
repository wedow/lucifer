"""function defaults kwonly."""
from support import loops


def run():
    def f(a, /, b=2, *, c=3): return a, b, c
    out = [f(1), f(1, c=7), f(1, **{'b': 8, 'c': 9})]
    f.__defaults__ = (11,); f.__kwdefaults__['c'] = 12
    return out, f(1)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
