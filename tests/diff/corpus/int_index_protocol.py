"""int index protocol."""
from support import loops


def run():
    class Index:
        def __index__(self): return -2
    value = Index()
    return [10, 20, 30][value], hex(value), bin(value), tuple(range(5))[value:]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
