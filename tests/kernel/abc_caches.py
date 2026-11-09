# ABCMeta over the _abc accelerator: registration, both caches and their
# invalidation, the subclass hook, abstract methods, and classes that die.
import abc, gc, collections.abc as cabc

class A(abc.ABC):
    @abc.abstractmethod
    def f(self): ...

class B(A):
    def f(self): return 1

class C: pass

out = []
for i in range(3):
    out.append((isinstance(B(), A), isinstance(C(), A), issubclass(C, A), issubclass(B, A)))
A.register(C)
out.append((isinstance(C(), A), issubclass(C, A), A.register(C) is C))
try:
    A()
except TypeError as e:
    out.append(str(e))
out.append(sorted(A.__abstractmethods__))
try:
    B.register(A)
except RuntimeError as e:
    out.append(str(e))
try:
    A.register(1)
except TypeError as e:
    out.append(str(e))
try:
    issubclass(1, A)
except TypeError as e:
    out.append(str(e))

class Hook(abc.ABC):
    @classmethod
    def __subclasshook__(cls, sub):
        return True if hasattr(sub, 'quack') else NotImplemented

class Duck:
    quack = 1
out.append((issubclass(Duck, Hook), issubclass(int, Hook), isinstance(Duck(), Hook)))
tok = abc.get_cache_token()
Hook.register(int)
out.append((issubclass(int, Hook), abc.get_cache_token() != tok))
reg, cache, neg, ver = A._abc_impl and abc._get_dump(A)
out.append((len(reg), type(next(iter(reg))).__name__, ver == abc.get_cache_token()))
A._abc_caches_clear()
A._abc_registry_clear()
out.append((issubclass(C, A), len(abc._get_dump(A)[0])))

def dead():
    class D: pass
    A.register(D)
    issubclass(D, A)
    return len(abc._get_dump(A)[0])
out.append(dead())
gc.collect()
out.append((len(abc._get_dump(A)[0]), len(abc._get_dump(A)[1])))
out.append(('__abc_tpflags__' in cabc.Sequence.__dict__, isinstance([], cabc.Sequence),
            isinstance({}, cabc.Mapping), isinstance('', cabc.Mapping), issubclass(dict, cabc.MutableMapping)))
match {'a': 1}:
    case {'a': x}:
        out.append(x)
match (1, 2):
    case [a, b]:
        out.append(a + b)
print(out)
