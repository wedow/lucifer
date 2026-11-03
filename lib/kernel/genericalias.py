"""types.GenericAlias and typing.Union (Objects/genericaliasobject.c,
Objects/unionobject.c), the __class_getitem__ of the builtin generic types
and the | of types.

An alias keeps four object fields in its native layout: origin, args,
parameters (None until asked for) and starred; a union keeps args,
hashable_args, unhashable_args (None when every member hashes) and
parameters. The kernel reaches them by index.
"""

from _lucifer import (
    alloc, field_get, field_set, static_type, is_subtype, kernel_get, kernel_set, kind,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
getset = _boot.getset
generic_getattr = kernel_get('generic_getattr')

__all__ = ()

_NoneType = type(None)
_GenericAlias = static_type('types.GenericAlias')
_Union = static_type('typing.Union')

_ORIGIN, _ARGS, _PARAMETERS, _STARRED = 0, 1, 2, 3
_U_ARGS, _U_HASHABLE, _U_UNHASHABLE, _U_PARAMETERS = 0, 1, 2, 3


def type_repr(p):
    """_Py_typing_type_repr: how a type argument prints inside an alias."""
    if p is Ellipsis:
        return '...'
    if p is _NoneType:
        return 'None'
    if hasattr(p, '__origin__') and hasattr(p, '__args__'):
        return repr(p)
    try:
        qualname = p.__qualname__
    except AttributeError:
        return repr(p)
    try:
        module = p.__module__
    except AttributeError:
        module = None
    if module is None:
        return repr(p)
    if isinstance(module, str) and module == 'builtins':
        return str(qualname)
    return str(module) + '.' + str(qualname)


# Parameters and substitution

def _index(items, item):
    """The position of item in items by identity, or -1."""
    for i, x in enumerate(items):
        if x is item:
            return i
    return -1


def _optional_attr(obj, name):
    try:
        return getattr(obj, name)
    except AttributeError:
        return None


def make_parameters(args):
    """_Py_make_parameters: the distinct type variables of args, in order."""
    if isinstance(args, list):
        args = tuple(args)
    parameters = []
    for t in args:
        if kind(t, 'type'):
            continue                # not the __parameters__ descriptor of a bare class
        if hasattr(t, '__typing_subst__'):
            if _index(parameters, t) < 0:
                parameters.append(t)
            continue
        subparams = _optional_attr(t, '__parameters__')
        if subparams is None and isinstance(t, (tuple, list)):
            subparams = make_parameters(t)
        if isinstance(subparams, tuple):
            for t2 in subparams:
                if _index(parameters, t2) < 0:
                    parameters.append(t2)
    return tuple(parameters)


def _subs_tvars(obj, params, argitems):
    """subs_tvars: obj with the type variables params replaced by argitems."""
    subparams = _optional_attr(obj, '__parameters__')
    if not isinstance(subparams, tuple) or not subparams:
        return obj
    subargs = []
    for arg in subparams:
        iparam = _index(params, arg)
        if iparam >= 0:
            if hasattr(type(params[iparam]), '__iter__') and isinstance(argitems[iparam], tuple):
                subargs.extend(argitems[iparam])          # a TypeVarTuple
                continue
            arg = argitems[iparam]
        subargs.append(arg)
    return obj[tuple(subargs)]


def _is_unpacked_typevartuple(arg):
    if kind(arg, 'type'):
        return False
    try:
        return bool(arg.__typing_is_unpacked_typevartuple__)
    except AttributeError:
        return False


def _unpacked_tuple_args(arg):
    if (type(arg) is _GenericAlias and field_get(arg, _STARRED)
            and field_get(arg, _ORIGIN) is tuple):
        return field_get(arg, _ARGS)
    return _optional_attr(arg, '__typing_unpacked_tuple_args__')


def _unpack_args(item):
    newargs = []
    for item in (item if isinstance(item, tuple) else (item,)):
        if not kind(item, 'type'):
            subargs = _unpacked_tuple_args(item)
            if (isinstance(subargs, tuple)
                    and not (subargs and subargs[-1] is Ellipsis)):
                newargs.extend(subargs)
                continue
        newargs.append(item)
    return tuple(newargs)


def subs_parameters(self, args, parameters, item):
    """_Py_subs_parameters: the args of self[item], replacing each of
    parameters by the matching item."""
    nparams = len(parameters)
    if nparams == 0:
        raise TypeError('%r is not a generic class' % (self,))
    item = _unpack_args(item)
    for param in parameters:
        prepare = _optional_attr(param, '__typing_prepare_subst__')
        if prepare is not None:
            item = prepare(self, item if isinstance(item, tuple) else (item,))
    argitems = item if isinstance(item, tuple) else (item,)
    nitems = len(argitems)
    if nitems != nparams:
        raise TypeError('Too %s arguments for %r; actual %d, expected %d' % (
            'many' if nitems > nparams else 'few', self, nitems, nparams))
    if isinstance(args, list):
        args = tuple(args)
    newargs = []
    for arg in args:
        if kind(arg, 'type'):
            newargs.append(arg)
            continue
        if isinstance(arg, (tuple, list)):          # recursively substitute in lists and tuples
            subargs = subs_parameters(self, arg, parameters, item)
            newargs.append(subargs if isinstance(arg, tuple) else list(subargs))
            continue
        unpack = _is_unpacked_typevartuple(arg)
        subst = _optional_attr(arg, '__typing_subst__')
        if subst is not None:
            iparam = _index(parameters, arg)
            if iparam < 0:
                raise TypeError('argument %r with __typing_subst__ was not found in __parameters__'
                                % (arg,))
            new = subst(argitems[iparam])
        else:
            new = _subs_tvars(arg, parameters, argitems)
        if unpack:
            if not isinstance(new, tuple):
                raise TypeError('expected __typing_subst__ of %s.%s objects to return a tuple, not %s.%s' % (
                    type(arg).__module__, type(arg).__qualname__,
                    type(new).__module__, type(new).__qualname__))
            newargs.extend(new)
        else:
            newargs.append(new)
    return tuple(newargs)


# types.GenericAlias

def make_alias(origin, args, starred=False):
    """Py_GenericAlias."""
    alias = alloc(_GenericAlias, _GenericAlias)
    return _setup_alias(alias, origin, args, starred)


def _setup_alias(alias, origin, args, starred=False):
    if not isinstance(args, tuple):
        args = (args,)
    field_set(alias, _ORIGIN, origin)
    field_set(alias, _ARGS, args)
    field_set(alias, _STARRED, starred)
    return alias


def _ga_parameters(self):
    parameters = field_get(self, _PARAMETERS)
    if parameters is None:
        parameters = make_parameters(field_get(self, _ARGS))
        field_set(self, _PARAMETERS, parameters)
    return parameters


def _readonly(self, value):
    raise AttributeError('readonly attribute')


_ATTR_BLOCKED = ('__bases__', '__copy__', '__deepcopy__')
_ATTR_EXCEPTIONS = ('__class__', '__origin__', '__args__', '__unpacked__', '__parameters__',
                    '__typing_unpacked_tuple_args__', '__mro_entries__', '__reduce_ex__',
                    '__reduce__')


def _set_orig_class(obj, alias):
    try:
        obj.__orig_class__ = alias
    except (AttributeError, TypeError):
        pass
    return obj


def _alias_eq(a, b):
    if field_get(a, _STARRED) != field_get(b, _STARRED):
        return False
    if not field_get(a, _ORIGIN) == field_get(b, _ORIGIN):
        return False
    return field_get(a, _ARGS) == field_get(b, _ARGS)


def _ga_items_list(p):
    return '[' + ', '.join([type_repr(item) for item in p]) + ']'


@kernel_type(_GenericAlias)
class GenericAlias:
    """Represent a PEP 585 generic type

For example, for t = list[int], t.__origin__ is list and t.__args__
is (int,)."""

    def __new__(cls, *args, **kwargs):
        if kwargs:
            raise TypeError('GenericAlias() takes no keyword arguments')
        if len(args) != 2:
            raise TypeError('GenericAlias expected 2 arguments, got %d' % len(args))
        return _setup_alias(alloc(cls, _GenericAlias), args[0], args[1])

    def __repr__(self):
        args = field_get(self, _ARGS)
        parts = [type_repr(field_get(self, _ORIGIN)), '[']
        for i, p in enumerate(args):
            if i:
                parts.append(', ')
            parts.append(_ga_items_list(p) if type(p) is list else type_repr(p))
        if not args:
            parts.append('()')
        parts.append(']')
        return ('*' if field_get(self, _STARRED) else '') + ''.join(parts)

    def __hash__(self):
        return hash(field_get(self, _ORIGIN)) ^ hash(field_get(self, _ARGS))

    def __eq__(self, other):
        if not is_subtype(type(other), _GenericAlias):
            return NotImplemented
        return _alias_eq(self, other)

    def __ne__(self, other):
        if not is_subtype(type(other), _GenericAlias):
            return NotImplemented
        return _alias_eq(self, other) is not True

    def __getitem__(self, item):
        newargs = subs_parameters(self, field_get(self, _ARGS), _ga_parameters(self), item)
        return make_alias(field_get(self, _ORIGIN), newargs, field_get(self, _STARRED))

    def __call__(self, *args, **kwargs):
        return _set_orig_class(field_get(self, _ORIGIN)(*args, **kwargs), self)

    def __getattribute__(self, name):
        if isinstance(name, str) and name not in _ATTR_BLOCKED and name not in _ATTR_EXCEPTIONS:
            return getattr(field_get(self, _ORIGIN), name)
        return generic_getattr(self, name)

    def __mro_entries__(self, bases):
        return (field_get(self, _ORIGIN),)

    def __instancecheck__(self, obj):
        raise TypeError('isinstance() argument 2 cannot be a parameterized generic')

    def __subclasscheck__(self, cls):
        raise TypeError('issubclass() argument 2 cannot be a parameterized generic')

    def __reduce__(self):
        origin, args = field_get(self, _ORIGIN), field_get(self, _ARGS)
        if field_get(self, _STARRED):
            return next, (iter(make_alias(origin, args)),)
        return type(self), (origin, args)

    def __dir__(self):
        names = dir(field_get(self, _ORIGIN))
        for name in _ATTR_EXCEPTIONS:
            if name not in names:
                names.append(name)
        return names

    def __iter__(self):
        return _AliasIterator(self)

    def __or__(self, other):
        return union_type_or(self, other)

    def __ror__(self, other):
        return union_type_or(other, self)

    __origin__ = getset(lambda self: field_get(self, _ORIGIN), _readonly)
    __args__ = getset(lambda self: field_get(self, _ARGS), _readonly)
    __unpacked__ = getset(lambda self: bool(field_get(self, _STARRED)), _readonly)
    __parameters__ = getset(_ga_parameters)
    __typing_unpacked_tuple_args__ = getset(
        lambda self: field_get(self, _ARGS)
        if field_get(self, _STARRED) and field_get(self, _ORIGIN) is tuple else None)


def _iter_init(self, alias):
    self._alias = alias


def _iter_self(self):
    return self


def _iter_next(self):
    alias = self._alias
    if alias is None:
        raise StopIteration
    self._alias = None
    return make_alias(field_get(alias, _ORIGIN), field_get(alias, _ARGS), True)


def _iter_reduce(self):
    return iter, ((self._alias,) if self._alias is not None else ((),))


# A class statement in a kernel module only yields a body, so the iterator is made with type().
_AliasIterator = type('generic_alias_iterator', (object,), {
    '__module__': 'builtins', '__slots__': ('_alias',), '__init__': _iter_init,
    '__iter__': _iter_self, '__next__': _iter_next, '__reduce__': _iter_reduce})


def _class_getitem(cls, item):
    return make_alias(cls, item)


for _cls in (list, dict, tuple, set, frozenset, enumerate, memoryview, classmethod, staticmethod,
             BaseExceptionGroup, static_type('generator'), static_type('coroutine'),
             static_type('async_generator'), static_type('mappingproxy'),
             static_type('weakref.ReferenceType')):
    kernel_type(_cls)(('', (), {'__class_getitem__': classmethod_(_class_getitem)}))

kernel_set('generic_alias', make_alias)


# typing.Union

# The type | accepts besides None, classes, aliases and unions:
# typing's TypeAliasType registers itself here.
_alias_types = []


def register_alias_type(cls):
    _alias_types.append(cls)


def _is_unionable(obj):
    if obj is None or kind(obj, 'type'):
        return True
    t = type(obj)
    return t is _Union or is_subtype(t, _GenericAlias) or t in _alias_types


def _type_check(arg, msg):
    if arg is None:
        return _NoneType
    if _is_unionable(arg):
        return arg
    return __import__('typing')._type_check(arg, msg)


def _add_single(arg, members, checked):
    """unionbuilder_add_single: members is (args, hashable set, unhashable list)."""
    if arg is None:
        arg = _NoneType
    elif type(arg) is _Union:
        for sub in field_get(arg, _U_ARGS):
            _add_single(sub, members, checked)
        return
    if checked:
        arg = _type_check(arg, 'Union[arg, ...]: each arg must be a type.')
    args, hashable, unhashable = members
    try:
        hash(arg)
    except Exception:
        if arg in unhashable:
            return
        unhashable.append(arg)
    else:
        if arg in hashable:
            return
        hashable.add(arg)
    args.append(arg)


def _make_union(members):
    args, hashable, unhashable = members
    if not args:
        raise TypeError('Cannot take a Union of no types.')
    if len(args) == 1:
        return args[0]
    union = alloc(_Union, _Union)
    field_set(union, _U_ARGS, tuple(args))
    field_set(union, _U_HASHABLE, frozenset(hashable))
    field_set(union, _U_UNHASHABLE, tuple(unhashable) if unhashable else None)
    return union


def _build(items, checked):
    members = ([], set(), [])
    for item in items:
        _add_single(item, members, checked)
    return _make_union(members)


def union_type_or(self, other):
    """_Py_union_type_or: self | other for types, aliases and None."""
    if not _is_unionable(self) or not _is_unionable(other):
        return NotImplemented
    return _build((self, other), False)


def _union_from_tuple(args):
    return _build(args if type(args) is tuple else (args,), True)


def _unions_equal(a, b):
    if not field_get(a, _U_HASHABLE) == field_get(b, _U_HASHABLE):
        return False
    ua, ub = field_get(a, _U_UNHASHABLE), field_get(b, _U_UNHASHABLE)
    if ua is not None and ub is not None:
        if len(ua) != len(ub):
            return False
        for x in ua:
            if x not in ub:
                return False
        for x in ub:
            if x not in ua:
                return False
        return True
    return ua is None and ub is None


def _union_parameters(self):
    parameters = field_get(self, _U_PARAMETERS)
    if parameters is None:
        parameters = make_parameters(field_get(self, _U_ARGS))
        field_set(self, _U_PARAMETERS, parameters)
    return parameters


@kernel_type(_Union)
class Union:
    """Represent a union type

E.g. for int | str"""

    def __repr__(self):
        return ' | '.join([type_repr(p) for p in field_get(self, _U_ARGS)])

    def __hash__(self):
        unhashable = field_get(self, _U_UNHASHABLE)
        if unhashable is not None:
            for arg in unhashable:
                hash(arg)
            raise TypeError('union contains %d unhashable elements' % len(unhashable))
        return hash(field_get(self, _U_HASHABLE))

    def __eq__(self, other):
        if type(other) is not _Union:
            return NotImplemented
        return _unions_equal(self, other)

    def __ne__(self, other):
        if type(other) is not _Union:
            return NotImplemented
        return not _unions_equal(self, other)

    def __getitem__(self, item):
        newargs = subs_parameters(self, field_get(self, _U_ARGS), _union_parameters(self), item)
        return _union_from_tuple(newargs)

    def __or__(self, other):
        return _build((self, other), True)

    def __ror__(self, other):
        return _build((other, self), True)

    def __getattribute__(self, name):
        if name == '__module__':
            return getattr(type(self), name)
        return generic_getattr(self, name)

    def __mro_entries__(self, bases):
        raise TypeError('Cannot subclass %r' % (self,))

    __class_getitem__ = classmethod_(lambda cls, args: _union_from_tuple(args))
    __args__ = getset(lambda self: field_get(self, _U_ARGS), _readonly)
    __parameters__ = getset(_union_parameters)
    __name__ = getset(lambda self: 'Union')
    __qualname__ = getset(lambda self: 'Union')
    __origin__ = getset(lambda self: _Union)


@kernel_type(type)
class _type_or:
    def __or__(self, other):
        return union_type_or(self, other)

    def __ror__(self, other):
        return union_type_or(other, self)
