"""unicode format map."""
from support import loops


def run():
    class D(dict):
        def __missing__(self, key): return '<' + key + '>'
    return '{name!r:>10}:{missing}'.format_map(D(name='é')), format('😀', '^7'), '%s:%04d' % ('é', 7)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
