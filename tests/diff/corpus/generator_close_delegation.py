"""generator close delegation."""
from support import loops


def run():
    log = []
    def child():
        try: yield 1
        finally: log.append('child')
    def parent():
        try: yield from child()
        finally: log.append('parent')
    g = parent(); next(g); g.close()
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
