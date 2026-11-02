"""dict colliding keys."""
from support import loops


def run():
    class Key:
        def __init__(self, n): self.n = n
        def __hash__(self): return 7
        def __eq__(self, other): return self.n == other.n
    d = {Key(i): i for i in range(12)}
    d[Key(4)] = 99; del d[Key(7)]
    return [(k.n, v) for k, v in d.items()], d[Key(4)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
