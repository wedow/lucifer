"""exception reraise identity."""
from support import loops


def run():
    outer = ValueError('same')
    try:
        try: raise outer
        except ValueError:
            try: raise
            except ValueError as inner: return inner is outer, inner.__context__
    finally: pass


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
