"""Another thread rebinds a global, monkeypatches a method and changes a class's base while a hot
loop reads them, one at a time; the iteration after the loop sees the patcher's flag sees the change.
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
        self.phase = 0
        self.seen = 0


def spin(state, helper, leaf, out):
    i = 0
    phase = seen = 0
    while True:
        a = g()
        b = helper.compute()
        c = Helper.compute(helper)
        d = Leaf.x
        e = leaf.x
        if phase != seen:
            out.append((phase, (a, b, c, d, e)))
            seen = state.seen = phase
            if phase == 3:
                return
        i += 1
        state.count = i
        phase = state.phase


def wait(state, phase):
    while state.seen != phase:
        time.sleep(0.001)
    start = state.count
    while state.count < start + 20000:
        time.sleep(0.001)


def run():
    global g
    gc.disable()
    state = State()
    out = []
    t = threading.Thread(target=spin, args=(state, Helper(), Leaf(), out))
    t.start()
    wait(state, 0)
    g = g2
    state.phase = 1
    wait(state, 1)
    Helper.compute = lambda self: 20
    state.phase = 2
    wait(state, 2)
    Leaf.__bases__ = (Base2,)
    state.phase = 3
    t.join()
    g = g_first
    Helper.compute = compute_first
    Leaf.__bases__ = (Base1,)
    return out


g_first = g
compute_first = Helper.compute
# Every run must see every change; the first runs on facts kept between polls.
for _ in range(min(loops(), 4)):
    print(run())
