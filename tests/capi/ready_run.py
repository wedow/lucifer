import sys
import ready
from ready import Vec, Vec3, Locked, Counter, MyList


def show(f, *args):
    try:
        print(repr(f(*args)))
    except BaseException as e:
        print(type(e).__name__, e)


# PyType_Ready: names, bases, the dict's wrappers and descriptors
print(Vec.__name__, Vec.__module__, Vec.__qualname__, Vec.__mro__, Vec.__bases__, Vec.__base__)
print(Vec.__doc__, Vec3.__doc__ is None, Locked.__doc__)
for name in ('__add__', '__radd__', '__neg__', '__bool__', '__pow__', '__len__', '__getitem__', '__setitem__',
             '__delitem__', '__contains__', '__lt__', '__eq__', '__hash__', '__call__', '__iter__', '__repr__',
             '__new__', '__getattribute__', '__sub__', '__init__'):
    print(name, name in Vec.__dict__, type(Vec.__dict__.get(name)).__name__)
print(sorted(k for k in Vec.__dict__ if not k.startswith('__')))
print(type(Vec.__dict__['sum']).__name__, type(Vec.__dict__['x']).__name__,
      type(Vec.__dict__['norm2']).__name__, Vec.__dict__['x'].__doc__, Vec.__dict__['sum'].__doc__)
print(Vec3.__mro__, Vec3.__base__, '__add__' in Vec3.__dict__, sorted(k for k in Vec3.__dict__ if not k.startswith('__')))

# instances
v = Vec(1, 2)
w = Vec(3, 4)
print(v, repr(w), v + w, -v, bool(v), bool(Vec()), len(v), v[0], v[1], list(v), list(iter(w)))
print(v == v, v == w, v != w, v < w, v <= w, v > w, v >= w, v == 3, hash(v) == hash(Vec(1, 2)))
print(2.0 in v, 5.0 in v, v.x, v.y, v.tag, v.sum, v.magic, v.norm2(), v.scaled(2.0), Vec.origin(), Vec.twice(21))
print(v(1, 2, k=3), v(), pow(v, 2), pow(v, 2, 5), v.__add__(w), w.__radd__(v), Vec.__add__(v, w))
v[0] = 10.5
v.x = 11
v.sum = 20
print(v, v.sum)
show(setattr, v, 'tag', 1)
show(delattr, v, 'sum')
show(lambda: v[2])
show(lambda: v + 3)
show(lambda: 3 + v)
show(lambda: v < 3)
show(Vec, 'x')
show(Locked)
show(Counter, 'x')
show(lambda: Vec.__add__(3, v))
show(lambda: Vec.__add__(v))
show(lambda: v.__add__(w, w))
show(lambda: v.__add__(other=w))
show(lambda: v(1, 2)['a'])
show(lambda: delattr(v, 'x'))

# subclassing in C and in Python
v3 = Vec3(1, 2)
v3.z = 9
print(v3, v3.z, v3 + v, type(v3 + v3).__name__, isinstance(v3, Vec), len(v3), v3.sum)


class PyVec(Vec):
    def __neg__(self):
        return 'negated'

    def norm2(self):
        return 'overridden ' + str(super().norm2())


p = PyVec(3, 4)
print(p, -p, p.norm2(), p + p, type(p + p).__name__, len(p), list(p), p == PyVec(4, 3), p.__dict__)
p.extra = 5
print(p.extra, p.__dict__)

# instances die through their own tp_dealloc, and a subclass's through tp_free
before = ready.dealloc_count()
a1 = Vec(1, 2)
del a1
a2 = PyVec(1, 2)
a2.extra = [1]
del a2
a3 = Vec3(1, 2)
del a3
print(ready.dealloc_count() - before)

# iteration protocol, a list subclass
c = Counter(4)
print(list(c), list(Counter()), next(Counter(2)), iter(c) is c)
ml = MyList([3, 1, 2])
ml.append(0)
print(ml, len(ml), sorted(ml), type(ml).__mro__, isinstance(ml, list), ml + [9], ml == [3, 1, 2, 0])

# a subtype of a builtin whose tp_new defers to the builtin's
f = ready.FloatSub(2.5)
print(f, type(f).__name__, f + 1, isinstance(f, float), ready.FloatSub('3'), ready.FloatSub.__mro__)
show(ready.FloatSub, 'x')


class PyFloatSub(ready.FloatSub):
    pass


print(PyFloatSub(1.5), type(PyFloatSub(1.5)).__name__)

# a metatype nobody can call still makes classes
meta, made = ready.make_meta_class()
print(meta.__name__, made.__name__, type(made) is meta, isinstance(made, type), made.__mro__)
show(meta, 'X', (), {})
print(type(made()).__name__)

# a spec type with a managed dict and weakref list
import weakref
Managed = ready.make_managed()
mg = Managed()
mg.x = 5
mg.__dict__['y'] = 6
print(mg.x, mg.y, mg.__dict__, weakref.ref(mg)() is mg, Managed.__dictoffset__ != 0)

# sets, iterators, lists, bytes, weak references, codecs
print(ready.containers())
print(ready.codecs())

# sys, thread-specific storage, running code
import sys
sys.addaudithook(lambda event, args: print('audit', event, args) if event == 'ready.event' else None)
print(ready.runtime_services())

# capsules, imports
print(ready.capsule_roundtrip())
show(ready.capsule_import, 'datetime.datetime_CAPI')
show(ready.capsule_import, 'datetime.no_such_capsule')
show(ready.capsule_import, 'no_such_package.capsule')
print(ready.imports())

# strings
for s in ('abc', 'h\xe9llo', 'ab€b', 'a\U0001f600b'):
    print(ready.strings(s))

# errors, dicts, slices, locks, context variables, datetime, numbers, objects
print(ready.errors())
print(ready.dict_ops())
for sl, n in [(slice(None), 5), (slice(1, 4), 10), (slice(None, None, -1), 5), (slice(-2, None), 5),
              (slice(10, 2, -3), 20), (slice(2**70, -2**70, -1), 7)]:
    print(ready.slices(sl, n))
show(ready.slices, slice(0, 1, 0), 5)
show(ready.slices, slice('a', 1), 5)
print(ready.locks())
print(ready.contextvars())
print(ready.datetimes())
print(ready.numbers())
for o in ([1, 2, 3], iter([1, 2]), 5, 'text', range(7)):
    show(ready.objects, o)

# slots that delegate to object's own
d = ready.Delegate()
print(hash(d) == object.__hash__(d), repr(d) == object.__repr__(d), str(d) == object.__repr__(d),
      d == d, d == ready.Delegate(), d != ready.Delegate())

# PyErr_SetObject runs the exception class, so its __init__ does too
class Resolution(TypeError):
    def __init__(self, ufunc, dtypes=None):
        super().__init__(ufunc)
        self.ufunc, self.dtypes = ufunc, dtypes

for value in (('add', 'f8'), 'single', ('only',)):
    try:
        ready.raise_object(Resolution, value)
    except TypeError as e:
        print(type(e).__name__, e.args, e.ufunc, e.dtypes)

# PyErr_Format over a pending error formats with no error set, then replaces it
for o in ('ab', 'π'):
    show(ready.format_over, o)

# and so does a builtin class: its __init__ and __new__ set errno, value and the OSError subclass
for exc, value in ((StopIteration, 5), (StopIteration, None), (OSError, (2, 'gone')), (FileNotFoundError, (2, 'gone')),
                   (OSError, (2, 'gone', 'f.txt')), (KeyError, ('k',)), (SystemExit, 3)):
    try:
        ready.raise_object(exc, value)
    except BaseException as e:
        print(type(e).__name__, e.args, getattr(e, 'value', '-'), getattr(e, 'errno', '-'),
              getattr(e, 'filename', '-'), getattr(e, 'code', '-'))

# PySequence_Check asks for sq_item, which mp_subscript alone does not give
class Indexed:
    def __getitem__(self, i): return i
class Keyed(dict):
    def __getitem__(self, i): return i
class Both(list):
    pass
print([ready.is_sequence(o) for o in ([], (), 'a', b'', range(2), bytearray(), memoryview(b''), {}, set(), object(),
                                       5, ready.Mapped(), ready.Vec(), Indexed(), Keyed(), Both(), ready.MyList(),
                                       iter([]), {}.keys(), 1.5, None)])

# a doc that starts "name(signature)\n--\n\n" is a text signature and a doc
import inspect
for f in (ready.sigfunc, ready.badsig, ready.Vec.norm2, ready.Vec(1, 2).norm2, ready.Vec.scaled):
    print(f.__name__, repr(f.__doc__), repr(f.__text_signature__))
print(inspect.signature(ready.sigfunc), inspect.signature(ready.Vec.norm2))

# tp_hash = PyObject_HashNotImplemented makes the type unhashable and __hash__ None
import collections.abc
u = ready.Unhashable()
print(ready.Unhashable.__dict__.get('__hash__', 'missing'), isinstance(u, collections.abc.Hashable))
show(hash, u)
class SubU(ready.Unhashable):
    pass
class HashedSub(ready.Unhashable):
    def __hash__(self): return 7
print(SubU.__hash__, hash(HashedSub()))
show(hash, SubU())

# a tp_richcompare without a tp_hash does not inherit object's hash
c = ready.CmpOnly()
print(ready.CmpOnly.__dict__.get('__hash__', 'missing'), isinstance(c, collections.abc.Hashable))
show(hash, c)
class CmpSub(ready.CmpOnly):
    pass
class CmpHashed(ready.CmpOnly):
    def __hash__(self): return 9
show(hash, CmpSub())
print(hash(CmpHashed()))

# a spec type made with only one of the pair gets an unhashable type or object's compare, not both inherited
SpecCmp, SpecHash = ready.make_half_pairs()
print(SpecCmp.__dict__.get('__hash__', 'missing'), sorted(k for k in SpecCmp.__dict__ if k in ('__eq__', '__hash__')),
      sorted(k for k in SpecHash.__dict__ if k in ('__eq__', '__hash__')))
show(hash, SpecCmp())
sh = SpecHash()
print(hash(sh), sh == sh, sh == SpecHash(), sh != sh)
class SpecCmpSub(SpecCmp):
    pass
show(hash, SpecCmpSub())

# Py_TPFLAGS_SEQUENCE and Py_TPFLAGS_MAPPING decide what a pattern matches
def kind(subject):
    match subject:
        case [*_]:
            return 'sequence'
        case {}:
            return 'mapping'
        case _:
            return 'neither'

print([kind(o) for o in (ready.Vec(1, 2), ready.MyList(), ready.Counter(), ready.CmpOnly(), ready.Mapped(), [1], {1: 2}, 'ab')])

# copied slots of a builtin run that builtin's methods on the subtype's instances
bc = ready.BytesCopy(b'ab')
print(bc == b'ab', bc == bytearray(b'ab'), bc.__eq__(5), bc.__lt__(b'b'), bc != 'ab', hash(bc) == hash(b'ab'),
      ready.BytesCopy.__eq__(bc, bytearray(b'ab')))

# builtin sequences repeat through sq_repeat and have no nb_multiply; a class defining __mul__ has the reverse
class Repeats:
    def __mul__(self, n): return ('repeated', n)
print([ready.seq_shape(o) for o in ([], (), 'a', b'', bytearray(), 1, 1.5, {}, Repeats(), ready.Vec(1, 2), ready.Mul())])
print(ready.BytesMul.__mro__, ready.seq_shape(ready.BytesMul(b'x')))
print(ready.repeat_twice([1]), ready.repeat_twice((1,)), ready.repeat_twice('ab'), ready.repeat_twice(b'ab'),
      ready.repeat_twice(bytearray(b'ab')))

# a frame made with PyFrame_New takes a line number stored at f_lineno, and can be cleared
import traceback
try:
    ready.raise_from_frame(41)
except ValueError as e:
    tb = e.__traceback__.tb_next
    print(tb.tb_frame.f_code.co_name, tb.tb_frame.f_code.co_filename, tb.tb_frame.f_lineno, tb.tb_lineno)
    traceback.clear_frames(e.__traceback__)
    traceback.clear_frames(e.__traceback__)
    print(tb.tb_frame.f_locals, [s.name for s in traceback.extract_tb(e.__traceback__)])

# a spec type adds wrappers only for the slots its spec gives; a class made with the metatype is hashable
print(sorted(meta.__dict__), sorted(made.__dict__), made in {made: 1}, hash(made) == hash(made))

# a builtin that is a kernel function is not called through its ml_meth by C code that checks ml_flags
print([ready.call_direct(f, a) for f, a in ((len, [1, 2]), (ord, 'a'), (abs, -3), (ready.is_sequence, []), (repr, 5),
                                           ([].append, 1), (bool, 0), (str.isalpha, 'abc'), (str.upper, 'abc'), (list.copy, [1]), (dict.keys, {1: 2}))])

# tp_getattr and tp_setattr, the slots that take a C string, answer attribute access
oa = ready.OldAttr()
print(oa.magic, getattr(oa, 'magic'), hasattr(oa, 'magic'), hasattr(oa, 'other'), getattr(oa, 'other', 'dflt'))
oa.magic = 1
show(setattr, oa, 'other', 1)
show(getattr, oa, 'other')
# a static subtype inherits them; a class made from the type answers through object's generic access
oa = ready.OldAttrSub()
show(getattr, oa, 'magic')
show(getattr, oa, 'other')
show(setattr, oa, 'other', 1)
class OldAttrPy(ready.OldAttr):
    pass
oa = OldAttrPy()
oa.other = 5
print(oa.other, oa.__dict__, hasattr(oa, 'magic'))
del oa.other
show(getattr, oa, 'other')

# a static type's tp_doc that starts with a signature is its __text_signature__, and not its __doc__
print(ready.DocSig.__text_signature__, repr(ready.DocSig.__doc__), inspect.signature(ready.DocSig),
      ready.Vec.__text_signature__, ready.Counter.__text_signature__)

# PyObject_GetItem on a class asks for __class_getitem__
class Generic:
    def __class_getitem__(cls, item):
        return ('generic', cls.__name__, item)
class Plain:
    pass
for o, k in ((list, int), (type, int), (dict, (str, int)), (Generic, 5), (Plain, 5), ((7, 8, 9), 1), ('abc', 2),
             ((7, 8), 'x'), (5, 0), (Generic(), 0), ([1, 2, 3], slice(0, 2)), ({'a': 1}, 'a'), (ready.Mapped(), 'k'),
             (ready.Vec(1, 2), 1), ((1, 2), 2**80)):
    show(ready.subscript, o, k)
