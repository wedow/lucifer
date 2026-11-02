"""unicode search bounds."""
from support import loops


def run():
    s = 'ab😀ab'
    return [(a, b, s.find('', a, b), s.count('a', a, b), s.startswith('ab', a, b)) for a, b in ((0, 5), (9, 12), (-4, -1), (2, 1))]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
