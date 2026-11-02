"""augassign attribute order."""
from support import loops


def run():
    log = []
    class C:
        @property
        def x(self): log.append('get'); return 4
        @x.setter
        def x(self, value): log.append(('set', value))
    def owner(): log.append('owner'); return C()
    def rhs(): log.append('rhs'); return 3
    owner().x += rhs()
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
