"""augassign subscript order."""
from support import loops


def run():
    log = []
    class C:
        def __getitem__(self, key): log.append(('get', key)); return [1]
        def __setitem__(self, key, value): log.append(('set', key, value))
    def key(): log.append('key'); return 3
    def rhs(): log.append('rhs'); return [2]
    C()[key()] += rhs()
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
