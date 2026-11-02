"""dict reinsert order."""
from support import loops


def run():
    d = dict.fromkeys('abcd', 0)
    del d['b']; d['b'] = 9; d['a'] = 7
    return list(d.items()), list(reversed(d)), d.popitem(), list(d)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
