# A metaclass's __prepare__ can return any mapping: the class body stores
# and loads its names through it.
class Space(dict):
    def __init__(self):
        super().__init__()
        self.log = []

    def __setitem__(self, key, value):
        self.log.append(('set', key))
        super().__setitem__(key, value)

    def __delitem__(self, key):
        self.log.append(('del', key))
        super().__delitem__(key)


class Meta(type):
    @classmethod
    def __prepare__(mcs, name, bases, **kwds):
        return Space()

    def __new__(mcs, name, bases, ns, **kwds):
        cls = super().__new__(mcs, name, bases, dict(ns))
        cls.log = ns.log
        return cls


class A(metaclass=Meta):
    x = 1
    y = x + 1
    del x
    def f(self):
        return 3
    z = len('abc')
    try:
        x
    except NameError as e:
        err = str(e)

print(A.log)
print(A.y, A.z, A().f(), A.err, hasattr(A, 'x'))


class Plain:
    def __init__(self):
        self.items = {'magic': 42}

    def __getitem__(self, key):
        return self.items[key]

    def __setitem__(self, key, value):
        self.items[key] = value


class Meta3(type):
    @classmethod
    def __prepare__(mcs, name, bases):
        return 5


try:
    class C(metaclass=Meta3):
        pass
except TypeError as e:
    print(e)


class Meta2(type):
    @classmethod
    def __prepare__(mcs, name, bases):
        return Plain()

    def __new__(mcs, name, bases, ns):
        return super().__new__(mcs, name, bases, {})


class B(metaclass=Meta2):
    seen = magic
    print(seen)
