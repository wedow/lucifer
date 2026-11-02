"""int type change after warmup."""
from support import loops


def run():
    def step(x): return x + 1
    for i in range(loops()): step(i)
    return step(2**100), step(1.25), step(True)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
