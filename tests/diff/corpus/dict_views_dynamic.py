"""dict views dynamic."""
from support import loops


def run():
    d = {'a': 1}; keys, items = d.keys(), d.items(); d['b'] = 2
    return list(keys), list(items), sorted(keys & {'b', 'c'}), list(d | {'a': 9, 'c': 3})


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
