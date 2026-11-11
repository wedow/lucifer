"""An exception raised two inlined user frames deep and caught by the
compiled caller, in a hot loop: the traceback's frames and lines, the
frames' locals, and a raise of a fresh instance and of a class."""
import traceback
from support import loops


def inner(x):
    if x > 5:
        raise ValueError('inner %d' % x)
    return x


def middle(x):
    y = x + 1
    return inner(y) * 2


def outer(n):
    total = 0
    for i in range(n):
        try:
            total += middle(i % 9)
        except ValueError as e:
            tb = traceback.extract_tb(e.__traceback__)
            names = [(t.name, t.line) for t in tb]
            loc = e.__traceback__.tb_next.tb_next.tb_frame.f_locals
            total += len(names)
    return total, names, sorted(loc.items())


def run():
    return outer(400)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
