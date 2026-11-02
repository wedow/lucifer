"""generator throw finally."""
from support import loops


def run():
    log = []
    def gen():
        try:
            try: yield 1
            except ValueError as e: yield e.args
        finally: log.append('finally')
    g = gen(); out = [next(g), g.throw(ValueError('v'))]; g.close()
    return out, log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
