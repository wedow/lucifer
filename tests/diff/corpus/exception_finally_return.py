"""exception finally return."""
from support import loops


def run():
    log = []
    def f():
        try: return 1
        finally: log.append('finally')
    def g():
        try: raise ValueError('lost')
        finally: return 2
    return f(), g(), log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
