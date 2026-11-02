"""exception target cleared."""
from support import loops


def run():
    try: raise ValueError('x')
    except ValueError as e: saved = str(e)
    try: return e
    except UnboundLocalError as error: return saved, type(error).__name__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
