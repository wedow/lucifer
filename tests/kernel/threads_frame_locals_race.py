"""Threads read the locals of generators other threads run, through
gi_frame.f_locals, while each step rebinds a local to a new module. The
owner's store drops the old module's last reference, so a reader that takes
the value without holding the frame can take it as it dies and free it again.
Modules because CPython frees them only in a collection (deferred reference
counts): its unheld reads are safe for them, so the oracle is deterministic."""
import threading
from types import ModuleType

STEPS = 200000
READERS = 3


def gen():
    m = ModuleType('m')
    while True:
        m = ModuleType('m')
        yield


gens = [gen() for _ in range(2)]
done = threading.Event()


def run(g):
    for _ in range(STEPS):
        next(g)


def read(out):
    bad = 0
    while not done.is_set():
        for g in gens:
            for name, value in g.gi_frame.f_locals.items():
                if name == 'm' and type(value) is not ModuleType:
                    bad += 1
    out.append(bad)


owners = [threading.Thread(target=run, args=(g,)) for g in gens]
results = []
readers = [threading.Thread(target=read, args=(results,)) for _ in range(READERS)]
for t in readers + owners:
    t.start()
for t in owners:
    t.join()
done.set()
for t in readers:
    t.join()
print('bad values:', sum(results), [type(g.gi_frame.f_locals['m']).__name__ for g in gens])
