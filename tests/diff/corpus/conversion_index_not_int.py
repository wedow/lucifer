"""conversion index not int."""
from support import loops


def run():
    class C:
        def __int__(self): return 7
    class D:
        def __index__(self): return 8
    out = [int(C()), int(D()), [0] * D()]
    try: range(C())
    except TypeError as e: out.append(type(e).__name__)
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
