"""augassign notimplemented."""
from support import loops


def run():
    log = []
    class C:
        def __iadd__(self, other): log.append('iadd'); return NotImplemented
        def __add__(self, other): log.append('add'); return ('new', other)
    c = C(); c += 9
    return c, log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
