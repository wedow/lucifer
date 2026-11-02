"""weakref finalize call once."""
from support import loops


def run():
    import weakref
    log = []
    class C: pass
    c = C(); f = weakref.finalize(c, log.append, 'done')
    before = f.alive; f(); f()
    return before, f.alive, log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
