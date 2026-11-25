# Staticmethods, classmethods and functions read through a class (A.st(x),
# A.cm(x), cls.k inside a classmethod) and through an instance, in hot
# loops whose class, bases, metaclass or descriptor change half way: a
# method replaced on the class and on a base, a staticmethod replaced by a
# classmethod and by a plain function, the staticmethod's callable
# replaced in place, the metaclass gaining __getattribute__ or a data
# descriptor of the name, a class attribute rebound. Also what must not be
# taken as one of these: a classmethod on the metaclass, a partial, a
# callable object and a property kept on the class, a classmethod
# subclass, classmethods of a staticmethod and of a builtin, the implicit
# classmethods __init_subclass__ and __class_getitem__, and another thread
# replacing the method. Each loop prints where its result changed; each
# result must be the interpreter's.
import functools
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
MID = N // 2


def changes(name, step):
    """Run step(i) for i in range(N), with the change at MID made by step;
    print each result that differs from the one before it."""
    out, last = [], object()
    for i in range(N):
        try:
            r = step(i)
        except Exception as e:
            r = type(e).__name__
        if r != last:
            out.append((i, r))
            last = r
    print(name, out)


def show(v):
    if isinstance(v, type):
        return v.__name__
    return v if isinstance(v, (int, str, tuple)) else type(v).__name__ + ' instance'


class A:
    k = 1

    @staticmethod
    def st(a):
        return ('A.st', a >= 0)

    @classmethod
    def cm(cls, a):
        return (cls.__name__, 'A.cm', cls.k + (a < 0))

    def plain(self, a):
        return ('A.plain', show(self), a >= 0)


class B(A):
    pass


class C(A):
    @staticmethod
    def st(a):
        return ('C.st', a >= 0)

    @classmethod
    def cm(cls, a):
        return (cls.__name__, 'C.cm', cls.k + (a < 0))


def fresh():
    """A class pair of its own for each loop that changes one."""
    class P:
        k = 1

        @staticmethod
        def st(a):
            return ('P.st', a >= 0)

        @classmethod
        def cm(cls, a):
            return (cls.__name__, 'P.cm', cls.k + (a < 0))

    class Q(P):
        pass
    return P, Q


# Through the class and a subclass, inherited and overridden.
changes('class calls', lambda i: (A.st(i), B.st(i), C.st(i), A.cm(i), B.cm(i), C.cm(i),
                                  A.plain(A, i), B.plain('b', i)))
changes('instance calls', lambda i: (A().st(i), B().st(i), C().st(i), A().cm(i), B().cm(i), C().cm(i)))


def as_values(i):
    f, g, h = A.st, B.cm, C.cm
    return (f(i), g(i), h(i), f is A.__dict__['st'].__func__, g.__self__ is B, g.__func__ is A.__dict__['cm'].__func__,
            B.plain is A.plain, type(g).__name__)


changes('read, not called', as_values)


# Through instances: several classes at one site, an instance's own value
# shadowing the method, __class__ assigned, instances with no dict.
class SA:
    __slots__ = ()

    @staticmethod
    def st(a):
        return ('SA.st', a >= 0)

    @classmethod
    def cm(cls, a):
        return (cls.__name__, 'SA.cm', a >= 0)


class SB(SA):
    __slots__ = ()


def read(o, i):
    r = (o.st(i), o.cm(i), o.st, type(o.cm).__name__)
    return r[:2] + (r[2](0), r[3])


def by_instance(objs, change):
    def step(i):
        if i == MID:
            change(objs)
        return tuple(read(o, i) for o in objs)
    return step


def shadow(objs):
    objs[0].st = lambda a: ('own st', a >= 0)
    objs[0].cm = lambda a: ('own cm', a >= 0)


def reclass(objs):
    objs[0].__class__ = C


def nothing(objs):
    pass


for label, objs, change in (('shadowed', [A()], shadow), ('shadowed, two classes', [A(), C()], shadow),
                            ('__class__ assigned', [B()], reclass), ('no dict', [SA(), SB()], nothing),
                            ('four classes', [A(), B(), C(), SB()], reclass)):
    changes('instances, ' + label, by_instance(objs, change))


def replaced(on, target, value_at_mid, call):
    P, Q = fresh()
    cls = P if on == 'P' else Q

    def step(i):
        if i == MID:
            setattr(cls, target, value_at_mid())
        return call(P, Q, i)
    return step


def new_static():
    return staticmethod(lambda a: ('new.st', a >= 0))


def new_class():
    return classmethod(lambda cls, a: (cls.__name__, 'new.cm', a >= 0))


def new_function():
    return lambda a: ('function', a >= 0)


both = lambda P, Q, i: (P.st(i), Q.st(i), P.cm(i), Q.cm(i))
via_instance = lambda P, Q, i: (P().st(i), Q().st(i), P().cm(i), Q().cm(i))
for on in ('P', 'Q'):
    for target in ('st', 'cm'):
        for label, make in (('staticmethod', new_static), ('classmethod', new_class), ('function', new_function)):
            changes(f'{on}.{target} becomes a {label}', replaced(on, target, make, both))
            changes(f'{on}.{target} becomes a {label}, by instance', replaced(on, target, make, via_instance))


def deleted(on):
    P, Q = fresh()
    Q.st = staticmethod(lambda a: ('Q.st', a >= 0))
    cls = P if on == 'P' else Q

    def step(i):
        if i == MID:
            del cls.st
        return (P.st(i), Q.st(i))
    return step


changes('P.st deleted', deleted('P'))
changes('Q.st deleted, P.st shows', deleted('Q'))


def reinit(kind):
    P, Q = fresh()
    d = P.__dict__['st' if kind is staticmethod else 'cm']

    def step(i):
        if i == MID:
            d.__init__(lambda *a: ('reinit', len(a)))
        return (P.st(i), Q.st(i), P.cm(i), Q.cm(i), P().st(i), Q().cm(i))
    return step


changes('staticmethod re-initialised', reinit(staticmethod))
changes('classmethod re-initialised', reinit(classmethod))


def bases_assigned():
    P, Q = fresh()

    class R:
        @staticmethod
        def st(a):
            return ('R.st', a >= 0)

        @classmethod
        def cm(cls, a):
            return (cls.__name__, 'R.cm', a >= 0)

    def step(i):
        if i == MID:
            Q.__bases__ = (R,)
        return (Q.st(i), Q.cm(i))
    return step


changes('__bases__ assigned', bases_assigned())


# cls.k inside a classmethod: rebound on the class, on the subclass, on a base.
def rebound(on):
    P, Q = fresh()

    def step(i):
        if i == MID:
            setattr(P if on == 'P' else Q, 'k', 100)
        if i == MID + 10 and on == 'P':
            del P.k
        return (P.cm(i), Q.cm(i))
    return step


changes('P.k rebound, then deleted', rebound('P'))
changes('Q.k rebound', rebound('Q'))


# The metaclass gains __getattribute__, or a data descriptor of the name.
def meta_change(how):
    class Meta(type):
        pass

    class D(metaclass=Meta):
        k = 1

        @staticmethod
        def st(a):
            return ('D.st', a >= 0)

        @classmethod
        def cm(cls, a):
            return (cls.__name__, 'D.cm', cls.k + (a < 0))

    class E(D):
        pass

    def step(i):
        if i == MID:
            if how == 'getattribute':
                def ga(cls, name):
                    if name in ('st', 'cm'):
                        return lambda a: ('meta getattribute', name, a >= 0)
                    return type.__getattribute__(cls, name)
                Meta.__getattribute__ = ga
            elif how == 'data':
                Meta.st = property(lambda cls: lambda a: ('meta property', show(cls), a >= 0))
                Meta.cm = property(lambda cls: lambda a: ('meta property cm', show(cls), a >= 0))
            elif how == 'nondata':
                Meta.st = lambda cls, a: ('meta function', a >= 0)
            elif how == 'getattr':
                Meta.__getattr__ = lambda cls, name: ('meta getattr', name)
            elif how == 'k':
                Meta.k = property(lambda cls: 50)
        return (D.st(i), E.st(i), D.cm(i), E.cm(i))
    return step


for how in ('getattribute', 'data', 'nondata', 'getattr', 'k'):
    changes(f'metaclass gains {how}', meta_change(how))


# A classmethod and a method on a metaclass, which the class does not have.
class Meta2(type):
    @classmethod
    def mcm(mcls, a):
        return (mcls.__name__, 'mcm', a >= 0)

    def mm(cls, a):
        return (cls.__name__, 'mm', a >= 0)

    @staticmethod
    def mst(a):
        return ('mst', a >= 0)


class F(metaclass=Meta2):
    pass


class G(F):
    pass


changes('metaclass methods', lambda i: (F.mcm(i), G.mcm(i), F.mm(i), G.mm(i), F.mst(i), Meta2.mcm(i)))


# Things kept on a class that are not staticmethods, classmethods or functions.
class Odd:
    def __call__(self, *a):
        return ('callable', len(a))


class mycm(classmethod):
    def __get__(self, obj, typ=None):
        return lambda *a: ('mycm', len(a))


class mysm(staticmethod):
    pass


def f3(*a):
    return ('f3', tuple(show(x) for x in a))


class H:
    k = 'hk'
    part = functools.partial(f3, 'p')
    odd = Odd()
    prop = property(lambda self: ('prop', show(self)))
    sub = mycm(f3)
    subst = mysm(f3)
    cm_of_st = classmethod(staticmethod(f3))
    cm_of_builtin = classmethod(getattr)
    st_of_cm = staticmethod(classmethod(f3))
    st_of_builtin = staticmethod(len)


def odd_things(i):
    h = H()
    return (H.part(i & 0), h.part(i & 0), H.odd(i), h.odd(i), type(H.prop).__name__, h.prop,
            H.sub(i), h.sub(i), H.subst(i & 0), h.subst(i & 0), H.cm_of_st(i & 0), h.cm_of_st(i & 0),
            H.cm_of_builtin('k'), h.cm_of_builtin('k'), type(H.st_of_cm).__name__, H.st_of_builtin('ab'),
            h.st_of_builtin('abc'))


changes('other things on a class', odd_things)


def odd_changes(i):
    if i == MID:
        Odd.__get__ = lambda self, obj, typ: (lambda *a: ('Odd bound', len(a)))
        H.part = functools.partial(f3, 'q')
    return (H.odd(i), H().odd(i), H.part(i & 0))


changes('callable object gains __get__', odd_changes)


# The implicit classmethods.
seen = []


class I:
    def __init_subclass__(cls, **kw):
        seen.append(cls.__name__)

    def __class_getitem__(cls, item):
        return (cls.__name__, 'getitem', item)


class J(I):
    pass


def implicit(i):
    if i % 500 == 0:
        type('K%d' % i, (J,), {})
    J.__init_subclass__()
    return (I[i & 0], J[i & 0], I.__class_getitem__(i & 0), J.__class_getitem__(i & 0), seen[-1])


changes('implicit classmethods', implicit)
print('subclasses seen', len(seen), seen[:3])


# Another thread replaces the method while the loop calls it.
def threaded():
    P, Q = fresh()
    started = threading.Event()
    done = threading.Event()

    def replace():
        started.wait()
        P.st = staticmethod(lambda a: ('threaded', a >= 0))
        done.set()

    t = threading.Thread(target=replace)
    t.start()
    kinds = set()
    i = 0
    while True:
        if i == 100:
            started.set()
        finished = done.is_set()
        r = (P.st(i), Q.st(i))
        kinds.add(r[0][0])
        kinds.add(r[1][0])
        i += 1
        if finished and i > 200:
            break
    t.join()
    print('thread', sorted(kinds), P.st(1), Q.st(2), Q().st(3))


threaded()


# Classmethods of things other than a function, read without a call and
# kept: each must be a real bound method of the wrapped object and the class.
class Escapes:
    k = 1
    cm = classmethod(getattr)
    cs = classmethod(staticmethod(lambda *a: a[0].__name__))
    cp = classmethod(property(lambda self: 'p'))
    co = classmethod(Odd())
    cpart = classmethod(functools.partial(f3, 'part'))


def escaping():
    out = []
    for i in range(5000):
        out.append(Escapes.cm)
        out.append(Escapes().cs)
        out.append(Escapes.cp)
        out.append(Escapes().co)
        out.append(Escapes.cpart)
    m = out[-5:]
    kinds = sorted({(type(x).__name__, type(x.__func__).__name__, x.__self__.__name__) for x in out})
    print('escaping', kinds, m[0]('k'), m[1](), m[3](1, 2), m[4](3))
    try:
        m[2]()
    except TypeError as e:
        print('escaping property', e)


escaping()


# The instance's class gains __getattribute__, or __getattr__, half way.
def instance_class_change(how):
    P, Q = fresh()
    objs = [P(), Q()]

    def step(i):
        if i == MID:
            if how == 'getattribute':
                Q.__getattribute__ = lambda self, name: (lambda a: ('instance getattribute', name, a >= 0))
            else:
                P.__getattr__ = lambda self, name: 'never'
        return tuple((o.st(i), o.cm(i)) for o in objs)
    return step


changes('instance class gains __getattribute__', instance_class_change('getattribute'))
changes('instance class gains __getattr__', instance_class_change('getattr'))


# Another thread re-runs __init__ on the wrappers while the loop calls them.
def st_of(j):
    return lambda a: ('reinit st', j)


def cm_of(j):
    return lambda cls, a: ('reinit cm', cls.__name__, j)


def threaded_reinit():
    P, Q = fresh()
    sm, cm = P.__dict__['st'], P.__dict__['cm']
    # Every callable stays alive: a reader may hold the one __init__ replaces
    # (CPython's own wrappers free it at once).
    keep = [(sm.__func__, cm.__func__)] + [(st_of(j % 2), cm_of(j % 2)) for j in range(200)]
    keep.append((lambda a: ('final st', a >= 0), lambda cls, a: ('final cm', cls.__name__)))
    started = threading.Event()
    done = threading.Event()

    def reinit():
        started.wait()
        for f, g in keep:
            sm.__init__(f)
            cm.__init__(g)
        done.set()

    t = threading.Thread(target=reinit)
    t.start()
    kinds = set()
    i = 0
    o = Q()
    while True:
        if i == 100:
            started.set()
        finished = done.is_set()
        for r in (P.st(i), Q.st(i), P.cm(i), Q.cm(i), o.st(i), o.cm(i)):
            kinds.add(r[0])
        i += 1
        if finished and i > 200:
            break
    t.join()
    between = {'reinit st', 'reinit cm'}       # seen or not, as the threads interleave
    print('thread reinit', sorted(kinds - between), kinds <= between | {'P.st', 'P', 'Q', 'final st', 'final cm'},
          P.st(1), Q.cm(2), o.cm(3), o.st(4))


threaded_reinit()
