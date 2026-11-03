import abc
import io
import _collections_abc

# io.StringIO and BytesIO
s = io.StringIO('first\nsecond\n')
print(s.readline(), s.read(3), s.tell(), s.getvalue() == 'first\nsecond\n')
s.write('tail')
print(s.getvalue(), s.seek(0), list(s), s.closed, s.readable(), s.writable(), s.seekable())
s.close()
try:
    s.read()
except ValueError as e:
    print(e)
b = io.BytesIO(b'abc\ndef')
print(b.read(2), b.readline(), b.readinto(bytearray(2)), b.getvalue(), b.tell())
b.seek(0, io.SEEK_END)
b.write(b'!')
print(b.getvalue(), b.getbuffer().nbytes)
del b

# TextIOWrapper over BytesIO
raw = io.BytesIO()
t = io.TextIOWrapper(raw, encoding='utf-8', newline='')
t.write('héllo\nworld\r\n')
t.flush()
print(raw.getvalue(), t.encoding, t.line_buffering, t.newlines)
raw.seek(0)
t2 = io.TextIOWrapper(raw, encoding='utf-8')
print(t2.readline(), repr(t2.readline()), repr(t2.read()), t2.newlines)
t3 = io.TextIOWrapper(io.BytesIO(b'a,b\nc'), encoding='ascii', errors='strict')
print(list(t3), t3.detach().getvalue())

# the abstract classes
print(io.IOBase, io.RawIOBase, io.BufferedIOBase, io.TextIOBase)
print(io.StringIO.__mro__[1:], io.BytesIO.__mro__[1:], io.TextIOWrapper.__mro__[1:])
print(isinstance(io.StringIO(), io.TextIOBase), isinstance(io.BytesIO(), io.BufferedIOBase),
      issubclass(io.BytesIO, io.IOBase), isinstance(io.StringIO(), io.RawIOBase))
print(type(io.IOBase).__name__, type(io.IOBase) is abc.ABCMeta)
print(io.DEFAULT_BUFFER_SIZE, io.UnsupportedOperation.__mro__[1:3], io.SEEK_CUR)

# abc
class Shape(abc.ABC):
    @abc.abstractmethod
    def area(self):
        ...
    @property
    @abc.abstractmethod
    def name(self):
        ...
    @classmethod
    def make(cls):
        return cls()
print(sorted(Shape.__abstractmethods__), Shape.__abstractmethods__.__class__.__name__)
try:
    Shape()
except TypeError as e:
    print(e)
class Square(Shape):
    def area(self):
        return 4
    name = 'square'
print(Square().area(), Square.make().name, isinstance(Square(), Shape), issubclass(Square, Shape))
class Half(Shape):
    def area(self):
        return 1
try:
    Half()
except TypeError as e:
    print(e)
class Virtual:
    pass
Shape.register(Virtual)
print(isinstance(Virtual(), Shape), issubclass(Virtual, Shape), issubclass(int, Shape))
@Shape.register
class Decorated:
    pass
print(issubclass(Decorated, Shape))
try:
    Shape.register(3)
except TypeError as e:
    print(e)
try:
    Shape.register(Shape)
except RuntimeError as e:
    print(e)

class Hooked(abc.ABC):
    @classmethod
    def __subclasshook__(cls, sub):
        if hasattr(sub, 'quack'):
            return True
        return NotImplemented
class Duck:
    def quack(self): pass
print(issubclass(Duck, Hooked), issubclass(int, Hooked), isinstance(Duck(), Hooked))
print(type(Shape), Shape.__mro__, abc.get_cache_token() is not None)
class Meta(abc.ABCMeta):
    pass
class WithMeta(metaclass=Meta):
    @abc.abstractmethod
    def f(self): ...
print(WithMeta.__abstractmethods__, type(WithMeta).__name__)
@abc.abstractmethod
def fn(): pass
print(fn.__isabstractmethod__, abc.abstractclassmethod, abc.abstractproperty)

# collections.abc
ca = _collections_abc
print(isinstance([], ca.Sequence), isinstance({}, ca.Mapping), isinstance({1}, ca.Set), isinstance(iter([]), ca.Iterator))
print(isinstance(1, ca.Hashable), isinstance([], ca.Hashable), isinstance('x', ca.Collection))
print(issubclass(dict, ca.MutableMapping), issubclass(tuple, ca.MutableSequence), isinstance(range(3), ca.Sequence))
print(isinstance((x for x in ()), ca.Generator), isinstance({}.keys(), ca.KeysView), isinstance({}.items(), ca.ItemsView))
print(ca.Sequence[int], ca.Mapping[str, int], ca.Callable[..., int])
class MyMap(ca.Mapping):
    def __getitem__(self, k): return k
    def __iter__(self): return iter('ab')
    def __len__(self): return 2
m = MyMap()
print(dict(m), list(m.items()), 'a' in m, m == {'a': 'a', 'b': 'b'}, m.get('z', 0))
print(sorted(ca.__all__)[:5], len(ca.__all__))
print(ca.__name__, ca.Sequence.__module__, ca.Awaitable, ca.Buffer)
