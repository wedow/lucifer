"""conversion str repr protocol."""
from support import loops


def run():
    class C:
        def __str__(self): return 'string'
        def __repr__(self): return 'representation'
        def __format__(self, spec): return 'format:' + spec
    c = C()
    return str(c), repr(c), ascii(c), f'{c:>4}', f'{c!r}'


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
