"""Another thread monkeypatches a method, rebinds a global and changes a class's base while a hot
loop reads them; the loop's next iteration after it sees the patcher's flag sees every change.
Collection is off, so that no stop-the-world sends the loop out of compiled code meanwhile."""
import gc
import threading
import time

from support import loops


class Base1:
    x = 1


class Base2:
    x = 2


class Leaf(Base1):
    pass


class Helper:
    def compute(self):
        return 10


def g():
    return 100


def g2():
    return 200


class State:
    def __init__(self):
        self.count = 0
        self.done = False


def spin(state, helper, leaf, out):
    i = 0
    after = False
    while True:
        a = g()
        b = helper.compute()
        c = Helper.compute(helper)
        d = Leaf.x
        e = leaf.x
        if after:
            out.append((a, b, c, d, e))
            return
        i += 1
        state.count = i
        after = state.done


def run():
    global g
    gc.disable()
    state = State()
    out = []
    t = threading.Thread(target=spin, args=(state, Helper(), Leaf(), out))
    t.start()
    while state.count < 20000:
        time.sleep(0.001)
    g = g2
    Helper.compute = lambda self: 20
    Leaf.__bases__ = (Base2,)
    state.done = True
    t.join()
    g = g_first
    Helper.compute = compute_first
    Leaf.__bases__ = (Base1,)
    return out[0]


g_first = g
compute_first = Helper.compute
for _ in range(min(loops(), 4)):
    RESULT = run()
print(repr(RESULT))
