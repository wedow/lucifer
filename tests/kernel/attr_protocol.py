# Which attribute protocol a type uses (object's, type's, a module's, a
# Python hook, a C type's own slot), seen through the same load, store and
# delete sites before and after the protocol changes: each site runs in a
# loop long enough to specialize and to compile, then the class, a base, a
# metaclass or a module changes, and the same sites run again. Plain
# classes, ast node classes (whose base ast.AST has C slot wrappers of the
# generic functions), metaclasses, modules and module subclasses, wrappers
# copied into unrelated classes, descriptors of every kind, and isinstance
# answers that depend on __class__ and __getattribute__.
import ast
import collections
import sys
import types

N = 200


def attempt(f, *args):
    try:
        return f(*args)
    except (AttributeError, TypeError) as e:
        return type(e).__name__ + ': ' + str(e)


def hot(f, *args):
    """f(*args) N times; the distinct answers, in order of first sight."""
    seen = []
    for _ in range(N):
        r = attempt(f, *args)
        if r not in seen:
            seen.append(r)
    return seen


# The sites: one code object each, shared by every case below.
def load_a(o):
    return o.a


def load_b(o):
    return o.b


def load_missing(o):
    return o.missing


def load_prop(o):
    return o.prop


def load_slot(o):
    return o.s


def load_method(o):
    return o.meth()


def load_cls(o):
    return o.__class__.__name__


def store_a(o):
    o.a = 5
    return o.__dict__.get('a', 'not in dict') if hasattr(o, '__dict__') else 'no dict'


def store_slot(o):
    o.s = 7
    return o.s


def delete_c(o):
    o.c = 1
    del o.c
    return 'c' in getattr(o, '__dict__', {})


def builtins_get(o):
    return getattr(o, 'a'), getattr(o, 'missing', 'default'), hasattr(o, 'missing')


SITES = (load_a, load_b, load_missing, load_prop, load_method, load_cls, store_a, delete_c, builtins_get)


def survey(label, o, sites=SITES):
    for site in sites:
        print(label, site.__name__, hot(site, o))


def hook_getattribute(self, name):
    if name == 'a':
        return 'hooked-a'
    return object.__getattribute__(self, name)


def hook_getattr(self, name):
    return 'fallback-' + name


def hook_setattr(self, name, value):
    object.__setattr__(self, name, ('set', value))


def hook_delattr(self, name):
    object.__setattr__(self, 'deleted', name)


HOOKS = (('__getattribute__', hook_getattribute), ('__getattr__', hook_getattr),
         ('__setattr__', hook_setattr), ('__delattr__', hook_delattr))


# 1. Lookup order: a data descriptor of the type, the instance's dict, a
#    non-data descriptor, a class attribute; __getattr__ only on
#    AttributeError, including one from a property getter or an unset slot.
class NonData:
    def __get__(self, obj, owner=None):
        return 'nondata'


class Data:
    def __get__(self, obj, owner=None):
        return 'data'

    def __set__(self, obj, value):
        pass


class Order:
    a = Data()
    b = NonData()
    c = 'class'

    def meth(self):
        return 'meth'

    @property
    def prop(self):
        raise AttributeError('from the getter')


o = Order()
o.__dict__.update(a='dict-a', b='dict-b', c='dict-c')
survey('order', o)
Order.__getattr__ = hook_getattr
survey('order+getattr', o)
del Order.__getattr__
del o.__dict__['b']
survey('order-dict', o)


class Slotted:
    __slots__ = ('s', 'a')

    def __getattr__(self, name):
        return 'fallback-' + name


s = Slotted()
print('unset slot', hot(load_slot, s), hot(load_a, s))
s.s = 1
print('set slot', hot(load_slot, s), hot(store_slot, s))
del Slotted.__getattr__
del s.s
print('unset slot, no fallback', hot(load_slot, s), hot(store_slot, s))


# 2. A Python __getattribute__ that calls object.__getattribute__, with and
#    without __getattr__.
class Hooked:
    __getattribute__ = hook_getattribute

    def meth(self):
        return 'meth'


h = Hooked()
h.b = 'b'
survey('hooked', h)
Hooked.__getattr__ = hook_getattr
survey('hooked+getattr', h)
del Hooked.__getattribute__
survey('getattr only', h)
del Hooked.__getattr__
survey('neither', h)


# 3. Hooks added to and removed from a base while subclasses and their
#    instances exist; then through __bases__ and a custom mro().
def cycle(label, base, objs):
    for name, f in HOOKS:
        setattr(base, name, f)
        for k, obj in enumerate(objs):
            survey(f'{label} +{name} {k}', obj)
    for name, _ in HOOKS:
        delattr(base, name)
        for k, obj in enumerate(objs):
            survey(f'{label} -{name} {k}', obj, (load_a, load_missing, store_a, delete_c))


class Root:
    def meth(self):
        return 'meth'


class Mid(Root):
    pass


class Leaf(Mid):
    pass


def fresh(cls):
    obj = cls()
    obj.a = 'a'
    obj.b = 'b'
    return obj


cycle('plain', Root, [fresh(Mid), fresh(Leaf)])


class Fallback:
    def __getattr__(self, name):
        return 'mixin-' + name

    def __setattr__(self, name, value):
        object.__setattr__(self, name, ('mixin', value))


class Moving(Root):
    pass


mv = fresh(Moving)
survey('moving', mv)
Moving.__bases__ = (Fallback, Root)
survey('moving onto Fallback', mv)
Moving.__bases__ = (Root,)
survey('moving back', mv)


class Chooser(type):
    flip = False

    def mro(cls):
        default = super().mro()
        return [cls, Fallback, *default[1:]] if Chooser.flip else default


class Chosen(Root, metaclass=Chooser):
    pass


ch = fresh(Chosen)
survey('mro', ch)
Chooser.flip = True
Chosen.__bases__ = (Root,)
survey('mro with Fallback', ch)
Chooser.flip = False
Chosen.__bases__ = (Root,)
survey('mro back', ch)


# 4. ast node classes: their base's slot wrappers are the generic
#    functions. The same as 3, and stores from the node constructor.
class Node(ast.AST):
    _fields = ('a', 'b')

    def meth(self):
        return 'meth'


class SubNode(Node):
    pass


def node(cls):
    return cls(a='a', b='b')


survey('node', node(Node))
cycle('ast', Node, [node(Node), node(SubNode)])
survey('ast.Name', ast.Name('x', ast.Load()), (load_missing, store_a, delete_c, builtins_get))
mvn = node(SubNode)
SubNode.__bases__ = (Fallback, Node)
survey('ast onto Fallback', mvn)
SubNode.__bases__ = (Node,)
survey('ast back', mvn)
Node.__setattr__ = hook_setattr
print('constructor', Node(a=1).a, SubNode(b=2).b)
del Node.__setattr__
print('constructor', Node(a=1).a, SubNode(b=2).b)


# 7. Metaclasses with __getattribute__, __getattr__ and __setattr__: class
#    attribute loads and stores before and after.
class Meta(type):
    pass


class WithMeta(metaclass=Meta):
    a = 'class-a'
    b = 'class-b'

    def meth():
        return 'meth'


def class_load(c):
    return c.a, c.b


def class_missing(c):
    return c.missing


def class_store(c):
    c.x = 5
    return c.__dict__.get('x')


def meta_getattribute(cls, name):
    if name == 'a':
        return 'meta-a'
    return type.__getattribute__(cls, name)


def meta_getattr(cls, name):
    return 'meta-fallback-' + name


def meta_setattr(cls, name, value):
    type.__setattr__(cls, name, ('meta', value))


CLASS_SITES = (class_load, class_missing, class_store)
for label, name, f in (('none', None, None), ('getattr', '__getattr__', meta_getattr),
                       ('getattribute', '__getattribute__', meta_getattribute),
                       ('setattr', '__setattr__', meta_setattr)):
    if name is not None:
        setattr(Meta, name, f)
    for site in CLASS_SITES:
        print('meta', label, site.__name__, hot(site, WithMeta))
    if name is not None:
        delattr(Meta, name)
for site in CLASS_SITES:
    print('meta after', site.__name__, hot(site, WithMeta))


# 9. Modules: a module __getattr__ added and removed; module subclasses with
#    and without their own hooks; __class__ assigned between them.
mod = types.ModuleType('mod')
mod.a = 'mod-a'
mod.b = 'mod-b'
mod.meth = lambda: 'meth'
survey('module', mod, (load_a, load_missing, builtins_get, load_method))
mod.__getattr__ = lambda name: 'module-fallback-' + name
survey('module+getattr', mod, (load_a, load_missing, builtins_get))
del mod.__getattr__
survey('module-getattr', mod, (load_a, load_missing, builtins_get))


class ModSub(types.ModuleType):
    pass


class ModHooked(types.ModuleType):
    def __getattr__(self, name):
        return 'modsub-' + name


mod.__class__ = ModSub
survey('ModSub', mod, (load_a, load_missing, store_a, builtins_get))
mod.__class__ = ModHooked
survey('ModHooked', mod, (load_a, load_missing, store_a, builtins_get))
ModHooked.__getattribute__ = hook_getattribute
survey('ModHooked+getattribute', mod, (load_a, load_missing, builtins_get))
del ModHooked.__getattribute__
mod.__class__ = types.ModuleType
survey('module again', mod, (load_a, load_missing, builtins_get))


# 10. Wrappers copied into unrelated classes: a generic slot wrapper raises
#     TypeError, unless __getattr__ is present; type's and module's
#     __getattribute__ copied.
for dunder in ('__getattribute__', '__setattr__', '__delattr__'):
    Copy = type('Copy', (), {dunder: getattr(ast.AST, dunder)})
    survey('copied ' + dunder, Copy(), (load_a, store_a, delete_c, builtins_get))
WithGetattr = type('WithGetattr', (), {'__getattribute__': ast.AST.__getattribute__,
                                       '__getattr__': hook_getattr})
survey('copied with __getattr__', WithGetattr(), (load_a, load_missing, builtins_get))
for label, owner in (('type', type), ('module', types.ModuleType)):
    Fake = type('Fake', (), {'__getattribute__': owner.__getattribute__})
    survey('fake ' + label, Fake(), (load_a, load_missing, builtins_get))
for dunder in ('__setattr__', '__delattr__'):
    Copy = type('Copy', (), {dunder: getattr(ast.AST, dunder), '__getattr__': hook_getattr})
    survey('copied ' + dunder + ' with __getattr__', Copy(), (load_missing, store_a, delete_c, builtins_get))
# __getattr__ gained and lost mid-loop under a copied __getattribute__, and
# reached through a base.
Copied = type('Copied', (), {'__getattribute__': ast.AST.__getattribute__, 'a': 'class-a'})
copied = Copied()
survey('copied getattribute', copied, (load_a, load_missing, builtins_get))
Copied.__getattr__ = hook_getattr
survey('copied getattribute+getattr', copied, (load_a, load_missing, builtins_get))
del Copied.__getattr__
survey('copied getattribute-getattr', copied, (load_a, load_missing, builtins_get))
CopiedUnder = type('CopiedUnder', (Fallback,), {'__getattribute__': ast.AST.__getattribute__, 'a': 'class-a'})
survey('copied under a fallback', CopiedUnder(), (load_a, load_missing, builtins_get))


# 12. Descriptors: __slots__ members, C members and getsets, tuplegetters,
#     on a plain class, an ast class, under a hook and after it.
Point = collections.namedtuple('Point', 'x y')


class P(Point):
    def __getattr__(self, name):
        return 'p-' + name


def tuple_get(p):
    return p.x, p.y, p.z


def c_member(o):
    return o.start, o.stop


def c_getset(f):
    return f.__name__, f.__qualname__


def slot_get(o):
    return o.s


class SlotsPlain:
    __slots__ = ('s',)


class SlotsNode(ast.AST):
    __slots__ = ('s',)


p = P(1, 2)
print('tuplegetter', hot(tuple_get, p))
P.__getattribute__ = lambda self, name: 'hooked' if name == 'x' else object.__getattribute__(self, name)
print('tuplegetter hooked', hot(tuple_get, p))
del P.__getattribute__
print('tuplegetter after', hot(tuple_get, p))
print('member', hot(c_member, slice(1, 2)))
print('getset', hot(c_getset, survey))
for cls in (SlotsPlain, SlotsNode):
    obj = cls()
    obj.s = 's'
    print('slots', cls.__name__, hot(slot_get, obj), hot(store_slot, obj))
    cls.__getattribute__ = lambda self, name: 'hooked-' + name
    print('slots hooked', cls.__name__, hot(slot_get, obj))
    del cls.__getattribute__
    cls.__setattr__ = hook_setattr
    print('slots set hook', cls.__name__, hot(store_slot, obj))
    del cls.__setattr__
    print('slots after', cls.__name__, hot(slot_get, obj), hot(store_slot, obj))


# 13. __setattr__ and __delattr__ overridden one at a time: each runs for its
#     own operation only, on a __slots__ member and a dict attribute;
#     defined with the class and assigned later.
calls = []


def note_delattr(self, name):
    calls.append(('del', name))


def note_setattr(self, name, value):
    calls.append(('set', name))


def delete_slot(o):
    o.s = 1
    del o.s
    return hasattr(o, 's')


class DelOnly:
    __slots__ = ('s', '__dict__')
    __delattr__ = note_delattr


class SetOnly:
    __slots__ = ('s', '__dict__')
    __setattr__ = note_setattr


class Later:
    __slots__ = ('s', '__dict__')


for cls in (DelOnly, SetOnly, Later):
    calls.clear()
    print('one hook', cls.__name__, hot(delete_slot, cls()), hot(delete_c, cls()), len(calls), sorted(set(calls)))
Later.__delattr__ = note_delattr
calls.clear()
print('one hook later del', hot(delete_slot, Later()), hot(delete_c, Later()), len(calls), sorted(set(calls)))
del Later.__delattr__
Later.__setattr__ = note_setattr
calls.clear()
print('one hook later set', hot(delete_slot, Later()), hot(delete_c, Later()), len(calls), sorted(set(calls)))
del Later.__setattr__
print('one hook after', hot(delete_slot, Later()), hot(delete_c, Later()))


# 8. A class's metaclass changed under a hot class-attribute load.
class MetaA(type):
    pass


class MetaB(type):
    def __getattribute__(cls, name):
        return 'meta-b-' + name if name == 'k' else type.__getattribute__(cls, name)


class Switched(metaclass=MetaA):
    k = 'class-k'


def class_k(c):
    return c.k


print('metaclass change', hot(class_k, Switched))
Switched.__class__ = MetaB
print('metaclass change after', hot(class_k, Switched))
Switched.__class__ = MetaA
print('metaclass change back', hot(class_k, Switched))


# The new metaclass keeps type's lookup but has a data descriptor of the
# name, which comes before the class's own; or only a __getattr__.
class MetaProp(type):
    k = property(lambda cls: 'meta-prop-k')


class MetaFallback(type):
    def __getattr__(cls, name):
        return 'meta-fallback-' + name


def class_gone(c):
    return c.gone


print('metaclass data descriptor', hot(class_k, Switched), hot(class_gone, Switched))
Switched.__class__ = MetaProp
print('metaclass data descriptor after', hot(class_k, Switched), hot(class_gone, Switched))
Switched.__class__ = MetaFallback
print('metaclass fallback', hot(class_k, Switched), hot(class_gone, Switched))
Switched.__class__ = MetaA
print('metaclass fallback back', hot(class_k, Switched), hot(class_gone, Switched))


# 11. super() bypasses the instance's __getattribute__.
class Parent:
    def who(self):
        return 'parent'


class Child(Parent):
    def __getattribute__(self, name):
        return lambda: 'hooked-' + name

    def ask(self):
        return super().who()


print('super bypasses', hot(Child.ask, Child()), hot(lambda o: o.who(), Child()))


# 14. A descriptor with __set__ and no __delete__, and the reverse.
class SetOnlyDescr:
    def __get__(self, obj, owner=None):
        return 'got'

    def __set__(self, obj, value):
        calls.append(('descr-set', value))


class DelOnlyDescr:
    def __get__(self, obj, owner=None):
        return 'got'

    def __delete__(self, obj):
        calls.append(('descr-del',))


class Described:
    s = SetOnlyDescr()
    d = DelOnlyDescr()


def descr_ops(o):
    out = [o.s, o.d]
    o.s = 1
    for f in (lambda: setattr(o, 'd', 2), lambda: delattr(o, 's'), lambda: delattr(o, 'd')):
        out.append(attempt(f))
    return out


calls.clear()
print('descriptor halves', hot(descr_ops, Described()), len(calls), sorted(set(calls)))


# 15. isinstance negatives on classes with each protocol, and a __class__ that
#     lies, gained and lost.
def negative(o):
    return isinstance(o, int), isinstance(o, (str, bytes)), isinstance(o, Order)


class Liar:
    pass


for label, obj in (('plain', Root()), ('node', node(Node)), ('hooked', Hooked()), ('module', mod),
                   ('class', Root), ('liar', Liar()), ('fallback', Fallback()), ('module subclass', ModSub('m')),
                   ('class with a metaclass', WithMeta), ('copied', copied), ('copied under a fallback', CopiedUnder()),
                   ('fake type', Fake())):
    print('isinstance', label, hot(negative, obj))
Meta.__getattr__ = meta_getattr
print('isinstance class with a metaclass __getattr__', hot(negative, WithMeta))
del Meta.__getattr__


class PropertyLiar(Liar):
    __class__ = property(lambda self: Order)


print('isinstance liar by property', hot(negative, PropertyLiar()))
Liar.__getattribute__ = lambda self, name: Order if name == '__class__' else object.__getattribute__(self, name)
print('isinstance liar by hook', hot(negative, Liar()))
del Liar.__getattribute__
print('isinstance liar after', hot(negative, Liar()))


# The __class__ property gained and lost mid-loop, through a base: the
# negatives answered from the type must stop while it is there.
class Honest:
    pass


class Shifty(Honest):
    pass


print('isinstance property before', hot(negative, Shifty()))
Shifty.__bases__ = (PropertyLiar,)
print('isinstance property gained', hot(negative, Shifty()))
Shifty.__bases__ = (Honest,)
print('isinstance property lost', hot(negative, Shifty()))
Node.__getattribute__ = lambda self, name: Order if name == '__class__' else object.__getattribute__(self, name)
print('isinstance node by hook', hot(negative, node(Node)))
del Node.__getattribute__
print('isinstance node after', hot(negative, node(Node)))


# A property's setter at a hot store, on a class that stores with object's
# algorithm but deletes with its own hook, and on an ast class; then
# __setattr__ gained and lost.
def prop_get(self):
    return self._p


def prop_set(self, value):
    object.__setattr__(self, '_p', ('prop', value))


class PropDel:
    p = property(prop_get, prop_set)

    def __delattr__(self, name):
        calls.append(('del', name))


class PropNode(ast.AST):
    p = property(prop_get, prop_set)


def store_prop(o):
    o.p = 3
    return o.p


for cls in (PropDel, PropNode):
    o = cls()
    print('property store', cls.__name__, hot(store_prop, o))
    cls.__setattr__ = hook_setattr
    print('property store hooked', cls.__name__, hot(store_prop, o))
    del cls.__setattr__
    print('property store unhooked', cls.__name__, hot(store_prop, o))
sys.stdout.flush()
