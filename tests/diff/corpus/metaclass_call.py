"""metaclass call."""
from support import loops


def run():
    log = []
    class M(type):
        def __call__(cls, x): log.append(('call', x)); return super().__call__(x + 1)
    class C(metaclass=M):
        def __new__(cls, x): log.append(('new', x)); return super().__new__(cls)
        def __init__(self, x): self.x = x; log.append(('init', x))
    return C(4).x, log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
