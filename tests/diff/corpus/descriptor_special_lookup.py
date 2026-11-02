"""descriptor special lookup."""
from support import loops


def run():
    class C:
        def __len__(self): return 3
        def __getattribute__(self, name):
            if name == '__len__': return lambda: 9
            return object.__getattribute__(self, name)
    c = C(); c.__len__ = lambda: 20
    return len(c), c.__len__()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
