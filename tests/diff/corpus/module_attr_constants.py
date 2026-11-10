"""A module's attributes read in hot loops (math.sqrt, a function, a
constant): after the module rebinds them, deletes one (the AttributeError),
gains one through its __getattr__ (PEP 562), and one rebound on every
call."""
import math
import types
from support import loops

mod = types.ModuleType('mod')
mod.scale = 3
mod.f = lambda x: x + 1


def missing(name):
    if name == 'late':
        return 'from __getattr__'
    raise AttributeError(name)


mod.__getattr__ = missing


def use(n):
    s = 0.0
    for i in range(n):
        s += math.sqrt(i) + mod.f(i) * mod.scale
    return round(s, 6)


def counter(n):
    for i in range(n):
        mod.count = i
        _ = mod.count
    return mod.count


def run():
    out = [use(300)]
    mod.scale = 5
    mod.f = lambda x: x * 2
    out.append(use(300))
    del mod.scale
    try:
        use(10)
    except AttributeError as e:
        out.append(str(e))
    mod.scale = 1
    out.append(mod.late)
    out.append(counter(200))
    return out


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
