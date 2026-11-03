"""_typing (Modules/_typingmodule.c, Objects/typevarobject.c): the
primitives of typing.py. The functions named _make_* and _set_* are the
PEP 695 intrinsics (Python/intrinsics.c), which build type parameters and
aliases whose bound, constraints, default and value are evaluated on first
use."""

import sys as _sys
from _lucifer import MISSING as _MISSING, _args, genericalias as _genericalias
from _types import UnionType as Union


_LAZY = object()        # a bound, constraints, default or value not evaluated yet


def _idfunc(x, /):
    """Return x unchanged; typing.NewType.__call__ uses it."""
    return x


def _typing(name):
    """An attribute of typing.py, which imports this module and so is
    imported lazily."""
    return getattr(__import__('typing'), name)


def _caller_module():
    """The __name__ of the globals of the code that called a constructor:
    the first frame outside this module (functions of builtin modules may
    run without a frame of their own)."""
    frame = _sys._getframe(0)
    while frame.f_globals.get('__name__') == __name__:
        frame = frame.f_back
    return frame.f_globals.get('__name__')


def _final(cls):
    """Refuse subclasses, as a type without Py_TPFLAGS_BASETYPE does."""
    def __init_subclass__(sub, **kwargs):
        raise TypeError("type '%s.%s' is not an acceptable base type" % (cls.__module__, cls.__qualname__))
    cls.__init_subclass__ = classmethod(__init_subclass__)
    return cls


def _readonly(slot):
    """A read-only member backed by the slot attribute."""
    def get(self):
        return getattr(self, slot)

    def set(self, value):
        raise AttributeError('readonly attribute')
    return property(get, set)


def _parse(fname, args, kwargs, names, npositional, nrequired):
    """The values of an Argument Clinic signature of npositional positional
    and keyword-only parameters, _MISSING where absent."""
    if len(args) > npositional and len(names) > npositional:
        raise TypeError('%s() takes exactly %d positional argument%s (%d given)' % (
            fname, npositional, '' if npositional == 1 else 's', len(args)))
    values = _args.unpack(fname, args, kwargs, names, 0, nrequired)
    if 'name' in names and not isinstance(values[0], str):
        raise TypeError("%s() argument 'name' must be str, not %s" % (fname, type(values[0]).__name__))
    return values


def _type_check(arg, msg):
    """typing._type_check, short of importing typing for what it passes
    unchanged: None, classes, aliases and unions, and these primitives."""
    if arg is None:
        return type(None)
    if _genericalias._is_unionable(arg) or isinstance(arg, _PRIMITIVES):
        return arg
    return _typing('_type_check')(arg, msg)


def _make_union(left, right):
    """left | right as typing.Union, which also takes forward references."""
    msg = 'Union[arg, ...]: each arg must be a type.'
    return _genericalias._build((_type_check(left, msg), _type_check(right, msg)), False)


# NoDefault

class NoDefaultType:
    """NoDefaultType()
--

The type of the NoDefault singleton."""

    __module__ = 'builtins'
    __slots__ = ()

    def __new__(cls, *args, **kwargs):
        if args or kwargs:
            raise TypeError('NoDefaultType takes no arguments')
        return NoDefault

    def __repr__(self):
        return 'typing.NoDefault'

    def __reduce__(self):
        return 'NoDefault'


NoDefault = object.__new__(NoDefaultType)
_final(NoDefaultType)
del NoDefaultType                                  # reached as type(NoDefault)


# Type parameters

class _ConstEvaluator:
    """_ConstEvaluator()
--

Internal type for implementing evaluation functions."""

    __slots__ = ('_value',)

    def __init__(self, value):
        self._value = value

    def __repr__(self):
        return '<constevaluator %r>' % (self._value,)

    def __call__(self, format, /):
        value = self._value
        if format != 4:                                 # not annotationlib's STRING
            return value
        if isinstance(value, tuple):
            return '(' + ', '.join([_genericalias.type_repr(item) for item in value]) + ')'
        return _genericalias.type_repr(value)


def _evaluator(value):
    return None if value is None else _ConstEvaluator(value)


def _unpack(param):
    return _typing('Unpack')[param]


class _TypeParam:
    """What TypeVar, ParamSpec and TypeVarTuple share: a name, a default and
    a module taken from the caller."""

    __slots__ = ('_name', '_default', '_evaluate_default', '__dict__', '__weakref__')

    __name__ = _readonly('_name')

    def _init(self, name, default, module):
        self._name = name
        self._default = default
        self._evaluate_default = None
        self.__module__ = module

    @property
    def __default__(self):
        if self._default is _LAZY:
            self._default = self._evaluate_default()
        return self._default

    @property
    def evaluate_default(self):
        if self._evaluate_default is not None:
            return self._evaluate_default
        return _evaluator(self._default)

    def has_default(self):
        return self._default is not NoDefault

    def __reduce__(self):
        return self._name

    def __mro_entries__(self, bases):
        raise TypeError('Cannot subclass an instance of %s' % type(self).__name__)


class TypeVar(_TypeParam):
    """Type variable.

The preferred way to construct a type variable is via the dedicated
syntax for generic functions, classes, and type aliases::

    class Sequence[T]:  # T is a TypeVar
        ...

This syntax can also be used to create bound and constrained type
variables::

    # S is a TypeVar bound to str
    class StrSequence[S: str]:
        ...

    # A is a TypeVar constrained to str or bytes
    class StrOrBytesSequence[A: (str, bytes)]:
        ...

Type variables can also have defaults:

    class IntDefault[T = int]:
        ...

However, if desired, reusable type variables can also be constructed
manually, like so::

   T = TypeVar('T')  # Can be anything
   S = TypeVar('S', bound=str)  # Can be any subtype of str
   A = TypeVar('A', str, bytes)  # Must be exactly str or bytes
   D = TypeVar('D', default=int)  # Defaults to int

Type variables exist primarily for the benefit of static type
checkers.  They serve as the parameters for generic types as well
as for generic function and type alias definitions.

The variance of type variables is inferred by type checkers when they
are created through the type parameter syntax and when
``infer_variance=True`` is passed. Manually created type variables may
be explicitly marked covariant or contravariant by passing
``covariant=True`` or ``contravariant=True``. By default, manually
created type variables are invariant. See PEP 484 and PEP 695 for more
details.
"""

    __module__ = 'typing'
    __slots__ = ('_bound', '_constraints', '_evaluate_bound', '_evaluate_constraints',
                 '_covariant', '_contravariant', '_infer_variance')

    def __new__(cls, *args, **kwargs):
        names = ('name', 'bound', 'default', 'covariant', 'contravariant', 'infer_variance')
        name, bound, default, covariant, contravariant, infer_variance = _parse(
            'typevar', args[:1], kwargs, names, 1, 1)
        constraints = args[1:]
        covariant = covariant is not _MISSING and bool(covariant)
        contravariant = contravariant is not _MISSING and bool(contravariant)
        infer_variance = infer_variance is not _MISSING and bool(infer_variance)
        if covariant and contravariant:
            raise ValueError('Bivariant types are not supported.')
        if infer_variance and (covariant or contravariant):
            raise ValueError('Variance cannot be specified with infer_variance.')
        if bound is _MISSING or bound is None:
            bound = None
        else:
            bound = _type_check(bound, 'Bound must be a type.')
        if len(constraints) == 1:
            raise TypeError('A single constraint is not allowed')
        if not constraints:
            constraints = None
        elif bound is not None:
            raise TypeError('Constraints cannot be combined with bound=...')
        self = object.__new__(cls)
        self._bound = bound
        self._constraints = constraints
        self._evaluate_bound = self._evaluate_constraints = None
        self._covariant = covariant
        self._contravariant = contravariant
        self._infer_variance = infer_variance
        self._init(name, NoDefault if default is _MISSING else default, _caller_module())
        return self

    def __repr__(self):
        if self._infer_variance:
            return self._name
        return ('+' if self._covariant else '-' if self._contravariant else '~') + self._name

    __covariant__ = _readonly('_covariant')
    __contravariant__ = _readonly('_contravariant')
    __infer_variance__ = _readonly('_infer_variance')

    @property
    def __bound__(self):
        if self._bound is _LAZY:
            self._bound = self._evaluate_bound()
        return self._bound

    @property
    def __constraints__(self):
        if self._constraints is _LAZY:
            self._constraints = self._evaluate_constraints()
        return () if self._constraints is None else self._constraints

    @property
    def evaluate_bound(self):
        return self._evaluate_bound or _evaluator(self._bound)

    @property
    def evaluate_constraints(self):
        return self._evaluate_constraints or _evaluator(self._constraints)

    def __typing_subst__(self, arg, /):
        return _typing('_typevar_subst')(self, arg)

    def __typing_prepare_subst__(self, alias, args, /):
        try:
            i = alias.__parameters__.index(self)
        except ValueError:
            raise ValueError('sequence.index(x): x not in sequence') from None
        if i < len(args):
            return args                                  # we already have a value for our TypeVar
        if i == len(args) and self._default is not NoDefault:
            return args + (self.__default__,)
        raise TypeError('Too few arguments for %s; actual %d, expected at least %d' % (
            alias, len(args), i + 1))

    def __or__(self, other):
        return _make_union(self, other)

    def __ror__(self, other):
        return _make_union(other, self)


class _ParamSpecAttr:
    """What ParamSpecArgs and ParamSpecKwargs share: a reference back to
their ParamSpec."""

    __slots__ = ('_origin', '__weakref__')
    _suffix = None

    def __new__(cls, *args, **kwargs):
        (origin,) = _args.unpack('paramspec' + cls._suffix, args, kwargs, ('origin',), 0, 1)
        self = object.__new__(cls)
        self._origin = origin
        return self

    def __repr__(self):
        origin = self._origin
        if type(origin) is ParamSpec:
            return origin._name + '.' + self._suffix
        return '%r.%s' % (origin, self._suffix)

    def __eq__(self, other):
        if type(self) is not type(other):
            return NotImplemented
        return self._origin == other._origin

    def __ne__(self, other):
        if type(self) is not type(other):
            return NotImplemented
        return self._origin != other._origin

    __hash__ = None
    __origin__ = _readonly('_origin')

    def __mro_entries__(self, bases):
        raise TypeError('Cannot subclass an instance of %s' % type(self).__name__)


class ParamSpecArgs(_ParamSpecAttr):
    """The args for a ParamSpec object.

Given a ParamSpec object P, P.args is an instance of ParamSpecArgs.

ParamSpecArgs objects have a reference back to their ParamSpec::

    >>> P = ParamSpec("P")
    >>> P.args.__origin__ is P
    True

This type is meant for runtime introspection and has no special meaning
to static type checkers.
"""

    __module__ = 'typing'
    __slots__ = ()
    _suffix = 'args'


class ParamSpecKwargs(_ParamSpecAttr):
    """The kwargs for a ParamSpec object.

Given a ParamSpec object P, P.kwargs is an instance of ParamSpecKwargs.

ParamSpecKwargs objects have a reference back to their ParamSpec::

    >>> P = ParamSpec("P")
    >>> P.kwargs.__origin__ is P
    True

This type is meant for runtime introspection and has no special meaning
to static type checkers.
"""

    __module__ = 'typing'
    __slots__ = ()
    _suffix = 'kwargs'


class ParamSpec(_TypeParam):
    """Parameter specification variable.

The preferred way to construct a parameter specification is via the
dedicated syntax for generic functions, classes, and type aliases,
where the use of '**' creates a parameter specification::

    type IntFunc[**P] = Callable[P, int]

The following syntax creates a parameter specification that defaults
to a callable accepting two positional-only arguments of types int
and str:

    type IntFuncDefault[**P = [int, str]] = Callable[P, int]

For compatibility with Python 3.11 and earlier, ParamSpec objects
can also be created as follows::

    P = ParamSpec('P')
    DefaultP = ParamSpec('DefaultP', default=[int, str])

Parameter specification variables exist primarily for the benefit of
static type checkers.  They are used to forward the parameter types of
one callable to another callable, a pattern commonly found in
higher-order functions and decorators.  They are only valid when used
in ``Concatenate``, or as the first argument to ``Callable``, or as
parameters for user-defined Generics. See class Generic for more
information on generic types.

An example for annotating a decorator::

    def add_logging[**P, T](f: Callable[P, T]) -> Callable[P, T]:
        '''A type-safe decorator to add logging to a function.'''
        def inner(*args: P.args, **kwargs: P.kwargs) -> T:
            logging.info(f'{f.__name__} was called')
            return f(*args, **kwargs)
        return inner

    @add_logging
    def add_two(x: float, y: float) -> float:
        '''Add two numbers together.'''
        return x + y

Parameter specification variables can be introspected. e.g.::

    >>> P = ParamSpec("P")
    >>> P.__name__
    'P'

Note that only parameter specification variables defined in the global
scope can be pickled.
"""

    __module__ = 'typing'
    __slots__ = ('_bound', '_covariant', '_contravariant', '_infer_variance')

    def __new__(cls, *args, **kwargs):
        names = ('name', 'bound', 'default', 'covariant', 'contravariant', 'infer_variance')
        name, bound, default, covariant, contravariant, infer_variance = _parse(
            'paramspec', args, kwargs, names, 1, 1)
        covariant = covariant is not _MISSING and bool(covariant)
        contravariant = contravariant is not _MISSING and bool(contravariant)
        infer_variance = infer_variance is not _MISSING and bool(infer_variance)
        if covariant and contravariant:
            raise ValueError('Bivariant types are not supported.')
        if infer_variance and (covariant or contravariant):
            raise ValueError('Variance cannot be specified with infer_variance.')
        self = object.__new__(cls)
        self._bound = None if bound is _MISSING else bound
        self._covariant = covariant
        self._contravariant = contravariant
        self._infer_variance = infer_variance
        self._init(name, NoDefault if default is _MISSING else default, _caller_module())
        return self

    def __repr__(self):
        if self._infer_variance:
            return self._name
        return ('+' if self._covariant else '-' if self._contravariant else '~') + self._name

    __bound__ = _readonly('_bound')
    __covariant__ = _readonly('_covariant')
    __contravariant__ = _readonly('_contravariant')
    __infer_variance__ = _readonly('_infer_variance')

    @property
    def args(self):
        """Represents positional arguments."""
        return ParamSpecArgs(self)

    @property
    def kwargs(self):
        """Represents keyword arguments."""
        return ParamSpecKwargs(self)

    def __typing_subst__(self, arg, /):
        return _typing('_paramspec_subst')(self, arg)

    def __typing_prepare_subst__(self, alias, args, /):
        return _typing('_paramspec_prepare_subst')(self, alias, args)

    def __or__(self, other):
        return _make_union(self, other)

    def __ror__(self, other):
        return _make_union(other, self)


class TypeVarTuple(_TypeParam):
    """Type variable tuple. A specialized form of type variable that enables
variadic generics.

The preferred way to construct a type variable tuple is via the
dedicated syntax for generic functions, classes, and type aliases,
where a single '*' indicates a type variable tuple::

    def move_first_element_to_last[T, *Ts](tup: tuple[T, *Ts]) -> tuple[*Ts, T]:
        return (*tup[1:], tup[0])

Type variables tuples can have default values:

    type AliasWithDefault[*Ts = (str, int)] = tuple[*Ts]

For compatibility with Python 3.11 and earlier, TypeVarTuple objects
can also be created as follows::

    Ts = TypeVarTuple('Ts')  # Can be given any name
    DefaultTs = TypeVarTuple('Ts', default=(str, int))

Just as a TypeVar (type variable) is a placeholder for a single type,
a TypeVarTuple is a placeholder for an *arbitrary* number of types. For
example, if we define a generic class using a TypeVarTuple::

    class C[*Ts]: ...

Then we can parameterize that class with an arbitrary number of type
arguments::

    C[int]       # Fine
    C[int, str]  # Also fine
    C[()]        # Even this is fine

For more details, see PEP 646.

Note that only TypeVarTuples defined in the global scope can be
pickled.
"""

    __module__ = 'typing'
    __slots__ = ()

    def __new__(cls, *args, **kwargs):
        name, default = _parse('typevartuple', args, kwargs, ('name', 'default'), 1, 1)
        self = object.__new__(cls)
        self._init(name, NoDefault if default is _MISSING else default, _caller_module())
        return self

    def __repr__(self):
        return self._name

    def __iter__(self):
        return iter((_unpack(self),))

    def __typing_subst__(self, arg, /):
        raise TypeError('Substitution of bare TypeVarTuple is not supported')

    def __typing_prepare_subst__(self, alias, args, /):
        return _typing('_typevartuple_prepare_subst')(self, alias, args)


_PRIMITIVES = (TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs, ParamSpecKwargs)


# Type aliases

class TypeAliasType:
    """Type alias.

Type aliases are created through the type statement::

    type Alias = int

In this example, Alias and int will be treated equivalently by static
type checkers.

At runtime, Alias is an instance of TypeAliasType. The __name__
attribute holds the name of the type alias. The value of the type alias
is stored in the __value__ attribute. It is evaluated lazily, so the
value is computed only if the attribute is accessed.

Type aliases can also be generic::

    type ListOrSet[T] = list[T] | set[T]

In this case, the type parameters of the alias are stored in the
__type_params__ attribute.

See PEP 695 for more information.
"""

    __module__ = 'typing'
    __slots__ = ('_name', '_type_params', '_value', '_compute_value', '__dict__')

    def __new__(cls, *args, **kwargs):
        if len(args) > 2:
            raise TypeError('typealias() takes exactly 2 positional arguments (%d given)' % len(args))
        name, value, type_params = _parse(
            'typealias', args, kwargs, ('name', 'value', 'type_params'), 2, 2)
        if type_params is _MISSING:
            type_params = ()
        elif not isinstance(type_params, tuple):
            raise TypeError('type_params must be a tuple')
        default_seen = False
        for param in type_params:
            if type(param) not in (TypeVar, ParamSpec, TypeVarTuple):
                raise TypeError('Expected a type param, got %r' % (param,))
            if param._default is NoDefault:
                if default_seen:
                    raise TypeError("non-default type parameter '%r' follows default type parameter" % (param,))
            else:
                default_seen = True
        self = object.__new__(cls)
        self._name = name
        self._type_params = type_params
        self._value = value
        self._compute_value = None
        self.__module__ = _caller_module()
        return self

    def __repr__(self):
        return self._name

    def __reduce__(self):
        return self._name

    def __getitem__(self, args):
        if not self._type_params:
            raise TypeError('Only generic type aliases are subscriptable')
        return _genericalias.make_alias(self, args)

    def __or__(self, other):
        return _genericalias.union_type_or(self, other)

    def __ror__(self, other):
        return _genericalias.union_type_or(other, self)

    def __iter__(self):
        return iter((_unpack(self),))

    __name__ = _readonly('_name')

    @property
    def __parameters__(self):
        # A TypeVarTuple must be unpacked when it is a parameter.
        return tuple([_unpack(p) if type(p) is TypeVarTuple else p for p in self._type_params])

    @property
    def __type_params__(self):
        return self._type_params

    @property
    def __value__(self):
        if self._value is _LAZY:
            self._value = self._compute_value()
        return self._value

    @property
    def evaluate_value(self):
        if self._compute_value is not None:
            return self._compute_value
        return _ConstEvaluator(self._value)


_genericalias.register_alias_type(TypeAliasType)
_final(TypeAliasType)


# Generic

class Generic:
    """Abstract base class for generic types.

On Python 3.12 and newer, generic classes implicitly inherit from
Generic when they declare a parameter list after the class's name::

    class Mapping[KT, VT]:
        def __getitem__(self, key: KT) -> VT:
            ...
        # Etc.

On older versions of Python, however, generic classes have to
explicitly inherit from Generic.

After a class has been declared to be generic, it can then be used as
follows::

    def lookup_name[KT, VT](mapping: Mapping[KT, VT], key: KT, default: VT) -> VT:
        try:
            return mapping[key]
        except KeyError:
            return default
"""

    __module__ = 'typing'
    __slots__ = ()

    def __class_getitem__(cls, *args, **kwargs):
        """Parameterizes a generic class.

At least, parameterizing a generic class is the *main* thing this
method does.  For example, for some generic class `Foo`, this is
called when we do `Foo[int]` - there, with `cls=Foo` and
`params=int`.

However, note that this method is also called when defining generic
classes in the first place with `class Foo[T]: ...`.
"""
        return _typing('_generic_class_getitem')(cls, *args, **kwargs)

    def __init_subclass__(cls, *args, **kwargs):
        """Function to initialize subclasses."""
        return _typing('_generic_init_subclass')(cls, *args, **kwargs)


for _cls in (TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs, ParamSpecKwargs):
    _final(_cls)


# The PEP 695 intrinsics: type parameters and aliases evaluated on first use.

def _make_typevar(name, evaluate_bound, evaluate_constraints):
    tv = object.__new__(TypeVar)
    tv._bound = _LAZY if evaluate_bound is not None else None
    tv._constraints = _LAZY if evaluate_constraints is not None else None
    tv._evaluate_bound = evaluate_bound
    tv._evaluate_constraints = evaluate_constraints
    tv._covariant = tv._contravariant = False
    tv._infer_variance = True
    tv._init(name, NoDefault, _caller_module())
    return tv


def _make_paramspec(name):
    ps = object.__new__(ParamSpec)
    ps._bound = None
    ps._covariant = ps._contravariant = False
    ps._infer_variance = True
    ps._init(name, NoDefault, _caller_module())
    return ps


def _make_typevartuple(name):
    tvt = object.__new__(TypeVarTuple)
    tvt._init(name, NoDefault, _caller_module())
    return tvt


def _set_typeparam_default(typeparam, evaluate_default):
    if type(typeparam) not in (TypeVar, ParamSpec, TypeVarTuple):
        raise TypeError('Expected a type param, got %r' % (typeparam,))
    typeparam._default = _LAZY
    typeparam._evaluate_default = evaluate_default
    return typeparam


def _make_typealias(args):
    name, type_params, compute_value = args
    alias = object.__new__(TypeAliasType)
    alias._name = name
    alias._type_params = () if type_params is None else type_params
    alias._value = _LAZY
    alias._compute_value = compute_value
    alias.__module__ = _caller_module()
    return alias


def _subscript_generic(params):
    """Generic[*params], with each TypeVarTuple unpacked."""
    params = tuple([_unpack(p) if type(p) is TypeVarTuple else p for p in params])
    return _typing('_GenericAlias')(Generic, params)
