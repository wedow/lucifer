"""Attribute sites that other threads respecialize while they run them
(luc-knb1). Readers load o.x where some instances of a class keep their
values in line and others have moved them to a dict, so the site turns
between an inline offset and a dict hint under one class version; writers
store o.y into instances of two classes that keep y at different offsets,
so the store site turns between their versions. A site's offset may be
used only with the version and opcode it was cached with: every load
returns the value its instance holds, and every store lands in its own
instance's y."""
import sys
import threading

STRESS = 'lucifer_stress' in sys._xoptions
ROUNDS = 2 if STRESS else 20
N = 2000


class A:
    x = None

    def __init__(self, i):
        self.x = i
        self.y = -1


class B:
    def __init__(self, i):
        self.y = -1
        self.x = i


bad = []


def reader(objs, stop):
    while not stop:
        for i, o in enumerate(objs):
            if o.x != i:
                bad.append(('load', i))


def writer(objs, stop, k):
    while True:
        for o in objs:
            o.y = k
        if stop:
            return


for r in range(ROUNDS):
    objs = [(A if i % 3 else B)(i) for i in range(N)]
    for o in objs[1::4]:
        vars(o)
    stop = []
    threads = [threading.Thread(target=reader, args=(objs, stop)) for _ in range(3)]
    threads += [threading.Thread(target=writer, args=(objs, stop, k)) for k in range(2)]
    for t in threads:
        t.start()
    for i, o in enumerate(objs):
        if i % 2 == 0:
            vars(o)['x'] = i
    stop.append(1)
    for t in threads:
        t.join()
    bad += [('store', i) for i, o in enumerate(objs) if o.x != i or o.y not in (0, 1)]
print('bad', bad[:3])
