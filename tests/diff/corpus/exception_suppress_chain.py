"""exception suppress chain."""
from support import loops


def run():
    try:
        try: 1 / 0
        except ZeroDivisionError: raise LookupError('x') from None
    except LookupError as e: return e.__cause__, type(e.__context__).__name__, e.__suppress_context__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
