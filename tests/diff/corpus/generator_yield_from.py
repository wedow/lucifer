"""generator yield from."""
from support import loops


def run():
    def child(): yield 1; return 7
    def parent():
        x = yield from child()
        yield x
    return list(parent())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
