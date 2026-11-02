"""generator pep479."""
from support import loops


def run():
    def gen():
        yield 1
        raise StopIteration('internal')
    g = gen(); next(g)
    try: next(g)
    except RuntimeError as e: return type(e).__name__, str(e), type(e.__cause__).__name__, e.__cause__.args


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
