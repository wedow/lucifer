"""conversion invalid protocol result."""
from support import loops


def run():
    class C:
        def __str__(self): return 1
        def __int__(self): return 'wrong'
    out = []
    for f in (str, int):
        try: f(C())
        except TypeError as e: out.append((type(e).__name__, str(e)))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
