"""context manager exception."""
from support import loops


def run():
    log = []
    class C:
        def __enter__(self): log.append('enter'); return 7
        def __exit__(self, typ, value, tb): log.append((typ.__name__, value.args, tb is not None)); return True
    with C() as x:
        log.append(x); raise ValueError('v')
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
