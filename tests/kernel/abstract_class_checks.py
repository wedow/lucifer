def check(func, *args):
    try:
        print(func(*args))
    except Exception as exc:
        print(type(exc).__name__, str(exc))


class Bases(tuple):
    def __iter__(self):
        raise AssertionError("called __iter__")

    def __len__(self):
        raise AssertionError("called __len__")

    def __getitem__(self, index):
        raise AssertionError("called __getitem__")


class Abstract:
    def __init__(self, name, bases=()):
        self.name = name
        self.bases = bases

    @property
    def __bases__(self):
        print("bases", self.name)
        return self.bases


root = Abstract("root")
middle = Abstract("middle", (root,))
leaf = Abstract("leaf", Bases((int, middle)))


class Proxy:
    @property
    def __class__(self):
        print("class", "proxy")
        return leaf


class MissingClass:
    @property
    def __class__(self):
        raise AttributeError("no class")


class BrokenBases:
    @property
    def __bases__(self):
        raise RuntimeError("broken bases")


class MissingBases:
    @property
    def __bases__(self):
        raise AttributeError("no bases")


class Hook:
    def __subclasscheck__(self, cls):
        return cls == 42

    def __instancecheck__(self, obj):
        return obj == 42


class Meta(type):
    def __instancecheck__(cls, obj):
        print("instance hook")
        return False


class Real(metaclass=Meta):
    pass


check(isinstance, 1, Bases((int, root)))
check(isinstance, 1, root)
check(isinstance, Proxy(), root)
check(isinstance, Proxy(), int)
check(isinstance, MissingClass(), root)
check(isinstance, MissingClass(), int)
check(isinstance, 1, Abstract("invalid", []))
check(isinstance, 1, MissingBases())
check(isinstance, 1, BrokenBases())
check(isinstance, Real(), (Real,))
check(isinstance, 42, Hook())

check(issubclass, root, int)
check(issubclass, int, root)
check(issubclass, root, root)
check(issubclass, leaf, root)
check(issubclass, leaf, int)
check(issubclass, leaf, Bases((int, BrokenBases())))
check(issubclass, 42, Hook())
check(issubclass, leaf, BrokenBases())
check(issubclass, BrokenBases(), 2)
check(issubclass, 1, BrokenBases())
check(issubclass, 1, 2)
check(issubclass, int, 2)
check(issubclass, 1, int)
check(issubclass, 1, ())
check(issubclass, 1, int | str)

check(int.__subclasscheck__, leaf)
check(int.__instancecheck__, MissingClass())
