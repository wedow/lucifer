# super() in hot compiled loops: method calls, super().__init__ with
# keywords in constructors, attribute reads (a plain value, a function
# read as a bound method, a property), super(C, self) with two arguments,
# super() in a classmethod; while the MRO changes: a method added to an
# intermediate class, the base's method replaced, __bases__ assigned, and
# self's __class__ assigned; a plain value whose class gains __get__ (from
# outside the compiled loop, and from a store the loop runs every iteration
# just before the read), a foreign self, self rebound before super(), the __class__ cell rebound,
# super shadowed by a global, a diamond and a base method called with
# several subclass receivers, a name set on an intermediate class, and
# super().__init__ reaching object.__init__ with an argument. Every value
# and error must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    tag = 'A'

    def __init__(self, x=0, *, y=1):
        self.x = x
        self.y = y

    def val(self, a):
        return a + 1

    @property
    def p(self):
        return self.x * 2

    @classmethod
    def make(cls, x):
        return cls(x)


class B(A):
    tag = 'B'


class C(B):
    def __init__(self, x, y=5):
        super().__init__(x, y=y)
        self.z = x + y

    def val(self, a):
        return super().val(a) * 2

    def reads(self):
        m = super().val
        return (super().tag, m(1), super().p, super(C, self).val(3), super(B, self).tag)

    @classmethod
    def make(cls, x):
        return super().make(x + 1)


class C2(C):
    def val(self, a):
        return a * 100


def calls(n):
    o = C(1)
    s = 0
    for _ in range(n):
        for j in range(10):
            s += o.val(j)
    return s


def constructors(n):
    s = 0
    for i in range(n):
        c = C(i, y=i % 3)
        s += c.x + c.y + c.z
    return s


def reads(n):
    o = C(4)
    last = None
    for _ in range(n):
        last = o.reads()
    return last


def classmethods(n):
    s = 0
    for i in range(n):
        s += C.make(i).x
    return s


def mro_changes(n):
    o = C(1)
    out = []
    for phase in range(5):
        if phase == 1:
            B.val = lambda self, a: a + 1000          # a method added to the intermediate class
        elif phase == 2:
            del B.val
            A.val = lambda self, a: a - 1             # the base's replaced
        elif phase == 3:
            class D(A):
                def val(self, a):
                    return a * 7
            B.__bases__ = (D,)
        elif phase == 4:
            o.__class__ = C2
        s = 0
        for j in range(n):
            s += o.val(j % 10)
        out.append(s)
    return out


class K:
    pass


class HasK:
    x = K()


class SubK(HasK):
    def get(self):
        return super().x


def get_gains_get(n):
    o = SubK()
    r = None
    for i in range(n):
        if i == n - 50:
            K.__get__ = lambda s, obj, typ: 'bound'
        r = o.get()
    del K.__get__
    return r


class Spare:
    pass


def get_gains_get_stored(n):
    # The store is compiled in the loop and kills the unit it runs in; the
    # read right after it in the same iteration must already see __get__.
    o = SubK()
    targets = [Spare] * n
    targets[n - 50] = K
    f = lambda s, obj, typ: 'bound'
    hits = 0
    for i in range(n):
        setattr(targets[i], '__get__', f)
        hits += o.get() == 'bound'
    del K.__get__
    return hits


class Foreign:
    def m(self):
        return 1


class UsesForeign(Foreign):
    def m(self):
        return super().m()


def foreign_self(n):
    errs = set()
    other = object()
    for i in range(n):
        try:
            UsesForeign.m(other if i % 100 == 0 else UsesForeign())
        except TypeError as e:
            errs.add(str(e))
    return sorted(errs)


class Rebinds(A):
    def val(self, a):
        if a == 7:
            self = A()
        return super().val(a)


def self_rebound(n):
    o = Rebinds()
    errs = set()
    s = 0
    for i in range(n):
        try:
            s += o.val(i % 10)
        except TypeError as e:
            errs.add(str(e))
    return s, sorted(errs)


class Cell(A):
    def val(self, a):
        return super().val(a) + 10


class Other(A):
    def val(self, a):
        return a * 1000


def class_cell_rebound(n):
    o = Cell()
    s = 0
    for i in range(n):
        if i == n // 2:
            Cell.val.__closure__[0].cell_contents = Other
        try:
            s += o.val(i % 10)
        except TypeError:
            s -= 1
    Cell.val.__closure__[0].cell_contents = Cell
    return s


def super_shadowed(n):
    g = globals()
    o = C(1)
    s = 0
    for i in range(n):
        if i == n - 100:
            g['super'] = lambda *a: A()
        s += o.val(i % 10)
    del g['super']
    return s


class D1(A):
    def val(self, a):
        return super().val(a) + 1


class D2(A):
    def val(self, a):
        return super().val(a) + 2


class Diamond(D1, D2):
    def val(self, a):
        return super().val(a) * 3


class E1(D1):
    pass


class E2(D2):
    pass


def receivers(n):
    objs = [D1(), D2(), Diamond(), E1(), E2()]
    s = 0
    for i in range(n):
        for o in objs:
            s += D1.val(o, i % 10) if isinstance(o, D1) else o.val(i % 10)
    setattr(B, 'val', lambda self, a: -a)
    for i in range(n):
        for o in objs:
            s += o.val(i % 10)
    del B.val
    return s


class Excess:
    def __init__(self, x):
        super().__init__(x)


def excess_init(n):
    errs = set()
    for i in range(n):
        try:
            Excess(i)
        except TypeError as e:
            errs.add(str(e))
    return sorted(errs)


print('calls', calls(N))
print('constructors', constructors(N))
print('reads', reads(N))
print('classmethods', classmethods(N))
print('mro changes', mro_changes(N))
print('get gains __get__', get_gains_get(N))
print('get gains __get__ stored', get_gains_get_stored(N))
print('foreign self', foreign_self(N))
print('self rebound', self_rebound(N))
print('class cell rebound', class_cell_rebound(N))
print('super shadowed', super_shadowed(N))
print('receivers', receivers(N))
print('excess init', excess_init(N))
