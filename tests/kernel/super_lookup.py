# super().name in its forms: functions bound to an instance or left as they are
# for a class, values that are no descriptors, descriptors of every kind, names
# found only on super itself, a __class__ that differs from the type, a type
# changed after a lookup, and the refusals. Each runs often enough to compile.


class Base:
    tag = 'base'
    items = [1, 2]

    def __init__(self, v):
        self.v = v

    def get(self):
        return ('Base.get', self.v)

    @classmethod
    def make(cls, v):
        return (cls.__name__, v)

    @staticmethod
    def static(x):
        return ('static', x)

    @property
    def prop(self):
        return ('prop', self.v)

    def __init_subclass__(cls, **kw):
        cls.seen = kw


class Left(Base, flag=1):
    def get(self):
        return ('Left',) + super().get()


class Right(Base):
    tag = 'right'

    def get(self):
        return ('Right',) + super().get()


class Both(Left, Right):
    def __init__(self, v):
        super().__init__(v * 10)

    def get(self):
        return ('Both',) + super().get()

    def tags(self):
        return super().tag, super(Left, self).tag, super(Right, self).tag, super().items

    @classmethod
    def make(cls, v):
        return ('Both.make',) + super().make(v)

    def others(self):
        return super().static(3), super().prop, super().__class__.__name__, super().__thisclass__.__name__


class Meta(type):
    def describe(cls):
        return ('Meta.describe', cls.__name__)


class Meta2(Meta):
    def describe(cls):
        return ('Meta2',) + super().describe()


class WithMeta(metaclass=Meta2):
    pass


class Liar:
    def get(self):
        return 'Liar.get'


class Sub(Liar):
    @property
    def __class__(self):
        return int

    def get(self):
        return ('Sub',) + (super().get(),)


def run(n):
    out = None
    for _ in range(n):
        b = Both(2)
        out = (b.v, b.get(), b.tags(), Both.make(4), b.others(), Both.seen, Left.seen,
               WithMeta.describe(), Sub().get())
    return out


for line in run(300):
    print(line)


class Changing(Base):
    def get(self):
        return super().get()


print(Changing(1).get())
Base.get = lambda self: ('replaced', self.v)
print(Changing(1).get())
del Base.get
try:
    Changing(1).get()
except AttributeError as e:
    print('AttributeError', e)


class Wrong:
    def get(self):
        return super(Base, self).get()


try:
    Wrong().get()
except TypeError as e:
    print('TypeError', e)


class Missing(Base):
    def get(self):
        return super().nothing


try:
    Missing(1).get()
except AttributeError as e:
    print('AttributeError', e)
