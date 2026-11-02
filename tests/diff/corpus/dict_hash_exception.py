"""dict hash exception."""
from support import loops


def run():
    class K:
        def __hash__(self): raise LookupError('hash broke')
    d = {'stable': 1}
    try: d[K()] = 2
    except LookupError as e: return type(e).__name__, e.args, list(d.items())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
