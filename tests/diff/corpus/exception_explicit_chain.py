"""exception explicit chain."""
from support import loops


def run():
    try:
        try: raise KeyError('inner')
        except KeyError as inner: raise ValueError('outer') from inner
    except ValueError as e:
        return e.args, type(e.__cause__).__name__, e.__cause__.args, e.__context__ is e.__cause__, e.__suppress_context__


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
