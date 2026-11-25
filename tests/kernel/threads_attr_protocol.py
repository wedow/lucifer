# One thread gives a base class, then takes away, __getattr__, __setattr__
# and __getattribute__, and a metaclass its __getattr__ and
# __getattribute__, with no synchronisation, while another loads, stores
# and misses attributes of instances of a subclass, and class attributes of
# a class of the metaclass, in hot loops: every answer is the one of the
# protocol before or after a flip, never a mixture.
import threading


class Base:
    pass


class Sub(Base):
    pass


class Meta(type):
    pass


class Owned(metaclass=Meta):
    a = 'class-a'


def hook_getattribute(self, name):
    v = object.__getattribute__(self, name)
    return 'hooked' if name == 'a' else v


def hook_getattr(self, name):
    return 'missing'


def hook_setattr(self, name, value):
    object.__setattr__(self, name, -value if type(value) is int else value)


def meta_getattribute(cls, name):
    v = type.__getattribute__(cls, name)
    return 'hooked' if name == 'a' else v


def meta_getattr(cls, name):
    return 'missing'


FLIPS = ((Base, '__getattribute__', hook_getattribute), (Base, '__getattr__', hook_getattr),
         (Base, '__setattr__', hook_setattr), (Meta, '__getattribute__', meta_getattribute),
         (Meta, '__getattr__', meta_getattr))


def flip(stop):
    k = 0
    while not stop.is_set():
        cls, name, f = FLIPS[k % len(FLIPS)]
        if name in cls.__dict__:
            delattr(cls, name)
        else:
            setattr(cls, name, f)
        k += 1


def main():
    n = 20000
    bad = 0
    stop = threading.Event()
    t = threading.Thread(target=flip, args=(stop,))
    t.start()
    try:
        s = Sub()
        s.a = 'a'
        for i in range(n):
            s.b = i
            try:
                miss = s.nope
            except AttributeError:
                miss = None
            try:
                cmiss = Owned.nope
            except AttributeError:
                cmiss = None
            good = (s.a in ('a', 'hooked') and s.b in (i, -i) and getattr(s, 'b') in (i, -i)
                    and miss in (None, 'missing') and getattr(s, 'nope', None) in (None, 'missing')
                    and Owned.a in ('class-a', 'hooked') and cmiss in (None, 'missing'))
            bad += not good
    finally:
        stop.set()
        t.join()
    for cls, name, _ in FLIPS:
        if name in cls.__dict__:
            delattr(cls, name)
    s = Sub()
    s.a = 'a'
    s.b = 5
    print(bad, s.a, s.b, getattr(s, 'nope', 'default'), Owned.a, getattr(Owned, 'nope', 'default'))


main()
