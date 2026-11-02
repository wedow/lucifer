"""augassign tuple item partial."""
from support import loops


def run():
    t = ([1],)
    try: t[0] += [2]
    except TypeError as e: return t, type(e).__name__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
