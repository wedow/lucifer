import _typing
import types
import sys
from _typing import (NoDefault, ParamSpec, ParamSpecArgs, ParamSpecKwargs, TypeAliasType,
                     TypeVar, TypeVarTuple, Generic)


def show(f):
    try:
        print(f())
    except BaseException as e:
        print(type(e).__name__, e)


print(sorted(n for n in dir(_typing) if not n.startswith('__')))
print(_typing.Union is types.UnionType, _typing._idfunc(5), _typing._idfunc(None))

# names and modules
for cls in (TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs, ParamSpecKwargs, TypeAliasType, Generic):
    print(cls.__module__, cls.__qualname__, cls.__name__)
print(type(NoDefault).__module__, type(NoDefault).__qualname__, repr(NoDefault), NoDefault.__reduce__())
print(type(NoDefault)() is NoDefault)
show(lambda: type(NoDefault)(1))
show(lambda: type(NoDefault)(a=1))

# TypeVar
T = TypeVar('T')
print(repr(T), T.__name__, T.__module__, T.__bound__, T.__constraints__, T.__default__)
print(T.__covariant__, T.__contravariant__, T.__infer_variance__, T.has_default())
print(T.evaluate_bound, T.evaluate_constraints, T.evaluate_default)
print(repr(TypeVar('C', covariant=True)), repr(TypeVar('D', contravariant=True)),
      repr(TypeVar('I', infer_variance=True)))
B = TypeVar('B', bound=int)
print(B.__bound__, B.evaluate_bound, B.evaluate_bound(1), B.evaluate_bound(4))
S = TypeVar('S', str, bytes)
print(S.__constraints__, S.evaluate_constraints, S.evaluate_constraints(4))
print(S.__bound__)
Df = TypeVar('Df', default=int)
print(Df.__default__, Df.has_default(), Df.evaluate_default)
print(T.__reduce__(), B.__reduce__())
print(TypeVar(name='K').__name__)
show(lambda: TypeVar())
show(lambda: TypeVar(1))
show(lambda: TypeVar('T', int))
show(lambda: TypeVar('T', int, str, bound=int))
show(lambda: TypeVar('T', bad=1))
show(lambda: TypeVar('T', covariant=True, contravariant=True))
show(lambda: TypeVar('T', covariant=True, infer_variance=True))
show(lambda: TypeVar.__mro_entries__(T, ()))
show(lambda: T.__mro_entries__(()))
show(lambda: T.nope)
show(lambda: setattr(T, '__name__', 'x'))
show(lambda: setattr(T, '__covariant__', True))
show(lambda: type('X', (TypeVar,), {}))
T.extra = 1
print(T.extra)
print(isinstance(T, TypeVar), type(T) is TypeVar)

# unions
u = T | int
print(type(u) is types.UnionType, u.__args__ == (T, int))
u = int | T
print(type(u) is types.UnionType, u.__args__ == (int, T))
print((T | None).__args__ == (T, type(None)))
print(len((T | T).__args__) if type(T | T) is types.UnionType else 'one')
print((TypeVar('X') | TypeVar('Y') | int).__args__[2:])

# TypeVar.__typing_prepare_subst__ without typing: defaults and arity
class FakeAlias:
    __parameters__ = (T, Df)

    def __repr__(self):
        return 'Alias'


print(Df.__typing_prepare_subst__(FakeAlias(), (str,)))
print(Df.__typing_prepare_subst__(FakeAlias(), (str, float)))
print(T.__typing_prepare_subst__(FakeAlias(), (str,)))
FakeAlias.__parameters__ = (T, TypeVar('Z'))
show(lambda: TypeVar('Z').__typing_prepare_subst__(FakeAlias(), ()))
show(lambda: FakeAlias.__parameters__[1].__typing_prepare_subst__(FakeAlias(), ()))

# ParamSpec
P = ParamSpec('P')
print(repr(P), P.__name__, P.__module__, P.__bound__, P.__default__, P.has_default())
print(P.__covariant__, P.__contravariant__, P.__infer_variance__, P.evaluate_default)
print(repr(P.args), repr(P.kwargs), type(P.args).__name__, type(P.kwargs).__name__)
print(P.args.__origin__ is P, P.kwargs.__origin__ is P)
print(P.args == P.args, P.args == P.kwargs, P.args != P.args, P.args == 1)
print(repr(ParamSpecArgs(3)), repr(ParamSpecKwargs('x')), repr(ParamSpecArgs(int)))
print(repr(ParamSpec('Q', covariant=True)), repr(ParamSpec('R', infer_variance=True)))
print(ParamSpec('Q', bound=int).__bound__, ParamSpec('Q', default=[int]).__default__)
print(ParamSpec('Q', default=[int]).has_default(), ParamSpec('Q', default=[int]).evaluate_default)
print(P.__reduce__())
print((P | int).__args__ == (P, int))
show(lambda: ParamSpec())
show(lambda: ParamSpec(1))
show(lambda: ParamSpec('P', 1))
show(lambda: ParamSpec('P', covariant=True, contravariant=True))
show(lambda: ParamSpec('P', covariant=True, infer_variance=True))
show(lambda: ParamSpecArgs())
show(lambda: ParamSpecArgs(1, 2))
show(lambda: hash(P.args))
show(lambda: P.args.__mro_entries__(()))
show(lambda: P.kwargs.__mro_entries__(()))
show(lambda: P.__mro_entries__(()))
show(lambda: setattr(P.args, '__origin__', 1))
show(lambda: type('X', (ParamSpecArgs,), {}))
show(lambda: type('X', (ParamSpecKwargs,), {}))
show(lambda: type('X', (ParamSpec,), {}))

# TypeVarTuple
Ts = TypeVarTuple('Ts')
print(repr(Ts), Ts.__name__, Ts.__module__, Ts.__default__, Ts.has_default(), Ts.evaluate_default)
print(TypeVarTuple('Us', default=(int, str)).__default__, TypeVarTuple('Us', default=(int,)).has_default())
print(Ts.__reduce__())
show(lambda: Ts.__typing_subst__(int))
show(lambda: TypeVarTuple())
show(lambda: TypeVarTuple(1))
show(lambda: TypeVarTuple('Ts', 1))
show(lambda: Ts.__mro_entries__(()))
show(lambda: type('X', (TypeVarTuple,), {}))

# TypeAliasType
A = TypeAliasType('A', list[int])
print(repr(A), A.__name__, A.__module__, A.__value__, A.__type_params__, A.__parameters__)
print(A.evaluate_value, A.evaluate_value(1), A.evaluate_value(4))
print(A.__reduce__())
show(lambda: A[int])
show(lambda: TypeAliasType('A', 2, type_params=3))
show(lambda: TypeAliasType('A', 2, 3))
show(lambda: TypeAliasType())
show(lambda: TypeAliasType('A'))
show(lambda: TypeAliasType(1, 2))
show(lambda: type('X', (TypeAliasType,), {}))
show(lambda: setattr(A, '__name__', 'B'))
print(TypeAliasType('N', int, type_params=()).__type_params__)
G = TypeAliasType('G', list[T], type_params=(T,))
print(G.__type_params__, G.__parameters__)
g = G[int]
print(type(g) is types.GenericAlias, g.__origin__ is G, g.__args__, repr(g))
print(repr(G[int, str]), G[T] == G[T], hash(G[T]) == hash(G[T]))
D2 = TypeAliasType('D2', int, type_params=(Df,))
print(D2.__parameters__)
show(lambda: TypeAliasType('Bad', int, type_params=(Df, T)))
show(lambda: TypeAliasType('Bad', int, type_params=(1,)))
show(lambda: TypeAliasType('Bad', int, type_params=(int,)))
TS = TypeAliasType('TS', tuple[int], type_params=(Ts,))
print(TS.__type_params__)
show(lambda: (A | int).__args__)
show(lambda: (int | A).__args__)
show(lambda: (A | None).__args__)
show(lambda: (A | str).__args__[0] is A)
show(lambda: (list[int] | A).__args__[1] is A)
show(lambda: A | 1)
show(lambda: 1 | A)

# Generic
print(Generic.__mro__ == (Generic, object), Generic.__slots__ if hasattr(Generic, '__slots__') else None)
print(Generic.__class_getitem__.__doc__[:30], Generic.__init_subclass__.__doc__)
print(Generic.__doc__[:30])
print(TypeVar.__doc__[:14], ParamSpec.__doc__[:30], TypeAliasType.__doc__[:11])

# the module of the creating code is the module of its globals
ns = {'__name__': 'elsewhere', 'TypeVar': TypeVar, 'ParamSpec': ParamSpec,
      'TypeVarTuple': TypeVarTuple, 'TypeAliasType': TypeAliasType}
exec('v = TypeVar("V"); p = ParamSpec("P"); t = TypeVarTuple("Ts"); a = TypeAliasType("A", int)', ns)
print(ns['v'].__module__, ns['p'].__module__, ns['t'].__module__, ns['a'].__module__)
ns2 = {'TypeVar': TypeVar}
exec('v = TypeVar("V")', ns2)
print(ns2['v'].__module__)
