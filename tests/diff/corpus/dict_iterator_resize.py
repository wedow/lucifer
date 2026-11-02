"""dict iterator resize."""
from support import loops


def run():
    d = {'a': 1, 'b': 2}; it = iter(d); first = next(it); d['c'] = 3
    try: next(it)
    except RuntimeError as e: return first, type(e).__name__, str(e), list(d)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
