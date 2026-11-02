"""generator reentry."""
from support import loops


def run():
    def gen():
        try: next(g)
        except ValueError as e: yield str(e)
    g = gen()
    return list(g)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
