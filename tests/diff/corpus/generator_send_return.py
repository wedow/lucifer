"""generator send return."""
from support import loops


def run():
    def gen():
        x = yield 1
        y = yield x * 2
        return y + 3
    g = gen(); out = [next(g), g.send(4)]
    try: g.send(10)
    except StopIteration as e: out.append(e.value)
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
