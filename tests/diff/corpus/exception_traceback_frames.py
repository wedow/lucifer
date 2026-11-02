"""exception traceback frames."""
from support import loops


def run():
    import traceback
    def a(): return b()
    def b(): raise RuntimeError('marker')
    try: a()
    except RuntimeError as e:
        frames = traceback.extract_tb(e.__traceback__)
        return [(f.name, f.lineno, f.line) for f in frames], ''.join(traceback.format_exception_only(e))


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
