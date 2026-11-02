"""dict equal distinct key."""
from support import loops


def run():
    class K:
        def __init__(self, label): self.label = label
        def __hash__(self): return 1
        def __eq__(self, other): return True
    a, b = K('first'), K('second'); d = {a: 1}; d[b] = 2
    return next(iter(d)) is a, d[a], len(d)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
