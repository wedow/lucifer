"""Descriptors and method binding (Objects/descrobject.c, funcobject.c,
classobject.c, typeobject.c's super): __get__, __set__ and __delete__ of
the descriptor types, function and method binding, property, classmethod,
staticmethod and super. Their layouts are native
(runtime/object/descr.c); fields are reached by index."""

from _lucifer import (
    MISSING, error, str_join, kind, type_lookup, type_dict, type_mro, type_tp_name,
    type_qualname, is_subtype, ns_get, alloc, static_type, descr_type, bind,
    method_new, member_get, member_set, getset_kernel, getset_get, getset_set,
    field_get, field_set, ns_set, super_args, is_callable, kernel_set, kernel_get, address, function_new,
    object_hash,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type
getset = _boot.getset
call_special = _boot.call_special

generic_getattr = kernel_get('generic_getattr')
generic_setattr = kernel_get('generic_setattr')
getattr_hook = kernel_get('getattr')

__all__ = ()

# Field indexes of the native layouts.
_FGET, _FSET, _FDEL, _DOC, _NAME = 0, 1, 2, 3, 4
_CALLABLE = 0
_THISCLASS, _SELF, _SELF_CLASS = 0, 1, 2


def _class_access(instance, owner):
    """Whether __get__ was called for the class: a None instance of the
    NoneType is an instance, which the C slots tell by a NULL pointer."""
    return instance is None and owner is not _NoneType


_NoneType = static_type('NoneType')
_Module = static_type('module')


def _quote(s):
    return str_join('', ("'", s, "'"))


def _descr_check(descr, obj):
    """descr_check: obj must be an instance of the descriptor's class."""
    owner = descr_type(descr)
    if not is_subtype(type(obj), owner):
        error(TypeError, str_join('', (
            "descriptor '", descr.__name__, "' for '", type_tp_name(owner),
            "' objects doesn't apply to a '", type_tp_name(type(obj)), "' object")))


@kernel_type(static_type('function'))
class function:
    def __new__(cls, /, *args, **kwargs):
        """Create a function object."""
        from _lucifer import _args  # loaded after this module
        code, globals, name, argdefs, closure, kwdefaults = _args.unpack(
            'function', args, kwargs,
            ('code', 'globals', 'name', 'argdefs', 'closure', 'kwdefaults'), 0, 2)
        return function_new(code, globals, None if name is MISSING else name,
                            None if argdefs is MISSING else argdefs,
                            None if closure is MISSING else closure,
                            None if kwdefaults is MISSING else kwdefaults)

    def __call__(self, /, *args, **kwargs):
        return self(*args, **kwargs)

    def __get__(self, instance, owner=None):
        if _class_access(instance, owner):
            return self
        return method_new(self, instance)

    def __repr__(self):
        return str_join('', ('<function ', self.__qualname__, ' at ', address(self), '>'))


@kernel_type(static_type('builtin_function_or_method'))
class builtin_function_or_method:
    def __reduce__(self):
        """meth_reduce: a module-level builtin pickles as its name, a bound one as getattr(self, name)."""
        owner = self.__self__
        if owner is None or is_subtype(type(owner), _Module):
            return self.__name__
        return (getattr, (owner, self.__name__))

    def __repr__(self):
        owner = self.__self__
        if owner is None or is_subtype(type(owner), _Module):
            return str_join('', ('<built-in function ', self.__name__, '>'))
        return str_join('', ('<built-in method ', self.__name__, ' of ', type_tp_name(type(owner)),
                             ' object at ', address(owner), '>'))


@kernel_type(static_type('method'))
class method:
    def __new__(cls, function, instance, /):
        if not is_callable(function):
            error(TypeError, 'first argument must be callable')
        if instance is None:
            error(TypeError, 'instance must not be None')
        return method_new(function, instance)

    def __getattribute__(self, name):
        """method_getattro: the method type's attributes, then the function's."""
        tp = type(self)
        descr = type_lookup(tp, name)
        if descr is not MISSING:
            get = type_lookup(type(descr), '__get__')
            if get is not MISSING:
                return get(descr, self, tp)
            return descr
        return getattr_hook(member_get(_method_func, self), name)

    def __get__(self, instance, owner=None):
        return self

    def __call__(self, /, *args, **kwargs):
        return member_get(_method_func, self)(member_get(_method_self, self), *args, **kwargs)

    def __repr__(self):
        func = member_get(_method_func, self)
        name = '?'
        for attr in ('__qualname__', '__name__'):
            try:
                found = getattr_hook(func, attr)
            except AttributeError:
                continue
            if kind(found, 'str'):
                name = found
            break
        return str_join('', ('<bound method ', name, ' of ', repr(member_get(_method_self, self)), '>'))

    def __eq__(self, other):
        if not kind_method(other):
            return NotImplemented
        return (member_get(_method_self, self) is member_get(_method_self, other)
                and member_get(_method_func, self) == member_get(_method_func, other))

    def __hash__(self):
        return object_hash(member_get(_method_self, self)) ^ hash(member_get(_method_func, self))

    def __reduce__(self):
        func = member_get(_method_func, self)
        return (getattr, (member_get(_method_self, self), getattr_hook(func, '__name__')))

    __doc__ = getset(lambda self: getattr_hook(member_get(_method_func, self), '__doc__'))


def kind_method(x):
    return type(x) is method


_method_func = ns_get(type_dict(method), '__func__', MISSING)
_method_self = ns_get(type_dict(method), '__self__', MISSING)


@kernel_type(static_type('method_descriptor'))
class method_descriptor:
    def __get__(self, instance, owner=None):
        if _class_access(instance, owner):
            return self
        _descr_check(self, instance)
        return bind(self, instance)


@kernel_type(static_type('classmethod_descriptor'))
class classmethod_descriptor:
    def __get__(self, instance, owner=None):
        if owner is None:
            owner = type(instance)
        cls = descr_type(self)
        if not kind(owner, 'type') or not is_subtype(owner, cls):
            error(TypeError, str_join('', (
                "descriptor '", self.__name__, "' for type '", type_tp_name(cls),
                "' doesn't apply to type '", type_tp_name(owner if kind(owner, 'type') else type(owner)),
                "'")))
        return bind(self, owner)


@kernel_type(static_type('wrapper_descriptor'))
class wrapper_descriptor:
    def __get__(self, instance, owner=None):
        if _class_access(instance, owner):
            return self
        _descr_check(self, instance)
        return bind(self, instance)


def _call(self, /, *args, **kwargs):
    return self(*args, **kwargs)


# The types the runtime calls natively also have a __call__ attribute. It goes
# into the dict alone: update_slots would make the slot call this function.
for _t in (builtin_function_or_method, method_descriptor, classmethod_descriptor,
           wrapper_descriptor, static_type('method-wrapper')):
    ns_set(type_dict(_t), '__call__', _call)


@kernel_type(static_type('member_descriptor'))
class member_descriptor:
    def __get__(self, instance, owner=None):
        if _class_access(instance, owner):
            return self
        _descr_check(self, instance)
        return member_get(self, instance)

    def __set__(self, instance, value):
        _descr_check(self, instance)
        member_set(self, instance, value)

    def __delete__(self, instance):
        _descr_check(self, instance)
        member_set(self, instance, MISSING)


@kernel_type(static_type('getset_descriptor'))
class getset_descriptor:
    def __get__(self, instance, owner=None):
        if _class_access(instance, owner):
            return self
        _descr_check(self, instance)
        functions = getset_kernel(self)
        if functions is MISSING:
            return getset_get(self, instance)
        fget, fset = functions
        return fget(instance)

    def __set__(self, instance, value):
        _descr_check(self, instance)
        _getset_store(self, instance, value)

    def __delete__(self, instance):
        _descr_check(self, instance)
        _getset_store(self, instance, MISSING)


def _getset_store(descr, instance, value):
    functions = getset_kernel(descr)
    if functions is MISSING:
        getset_set(descr, instance, value)
        return
    fget, fset = functions
    if fset is None:
        error(AttributeError, str_join('', (
            "attribute '", descr.__name__, "' of '", type_tp_name(descr_type(descr)),
            "' objects is not writable")))
    fset(instance, value)


def _property_error(prop, obj, what):
    name = field_get(prop, _NAME)
    if name is None:
        fget = field_get(prop, _FGET)
        name = fget.__name__ if fget is not None else None
    owner = _quote(type_qualname(type(obj)))
    if name is None:
        error(AttributeError, str_join('', ('property of ', owner, ' object has no ', what)))
    error(AttributeError, str_join('', (
        'property ', _quote(name), ' of ', owner, ' object has no ', what)))


def _property_get_name(self):
    name = field_get(self, _NAME)
    if name is not None:
        return name
    fget = field_get(self, _FGET)
    if fget is None:
        error(AttributeError, "'property' object has no attribute '__name__'")
    return fget.__name__


def _property_set_name(self, value):
    field_set(self, _NAME, None if value is MISSING else value)


def _property_abstract(self):
    for i in (_FGET, _FSET, _FDEL):
        f = field_get(self, i)
        if f is not None:
            try:
                if getattr_hook(f, '__isabstractmethod__'):
                    return True
            except AttributeError:
                pass
    return False


def _wrapped_abstract(self):
    """The __isabstractmethod__ of classmethod and staticmethod: their function's."""
    try:
        return getattr_hook(field_get(self, _CALLABLE), '__isabstractmethod__')
    except AttributeError:
        return False


@kernel_type(property)
class property:
    """Property attribute."""

    def __new__(cls, /, *args, **kwargs):
        return alloc(cls, property)

    def __init__(self, fget=None, fset=None, fdel=None, doc=None):
        if doc is None and fget is not None:
            doc = fget.__doc__
        field_set(self, _FGET, fget)
        field_set(self, _FSET, fset)
        field_set(self, _FDEL, fdel)
        field_set(self, _DOC, doc)

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        fget = field_get(self, _FGET)
        if fget is None:
            _property_error(self, instance, 'getter')
        return fget(instance)

    def __set__(self, instance, value):
        fset = field_get(self, _FSET)
        if fset is None:
            _property_error(self, instance, 'setter')
        fset(instance, value)

    def __delete__(self, instance):
        fdel = field_get(self, _FDEL)
        if fdel is None:
            _property_error(self, instance, 'deleter')
        fdel(instance)

    def __set_name__(self, owner, name):
        field_set(self, _NAME, name)

    def getter(self, fget):
        """Descriptor to obtain a copy of the property with a different getter."""
        return _property_copy(self, fget, MISSING, MISSING)

    def setter(self, fset):
        """Descriptor to obtain a copy of the property with a different setter."""
        return _property_copy(self, MISSING, fset, MISSING)

    def deleter(self, fdel):
        """Descriptor to obtain a copy of the property with a different deleter."""
        return _property_copy(self, MISSING, MISSING, fdel)

    __name__ = getset(_property_get_name, _property_set_name)
    __isabstractmethod__ = getset(_property_abstract)


def _property_copy(old, fget, fset, fdel):
    """property_copy: a doc taken from the old getter is taken anew."""
    old_fget = field_get(old, _FGET)
    doc = field_get(old, _DOC)
    if old_fget is not None and doc is old_fget.__doc__:
        doc = None
    new = type(old)(old_fget if fget is MISSING else fget,
                    field_get(old, _FSET) if fset is MISSING else fset,
                    field_get(old, _FDEL) if fdel is MISSING else fdel, doc)
    field_set(new, _NAME, field_get(old, _NAME))
    return new


def _wraps(wrapper, wrapped):
    """functools_wraps: the wrapped callable's name, qualname, module and doc."""
    for name in ('__module__', '__name__', '__qualname__', '__doc__'):
        try:
            value = getattr_hook(wrapped, name)
        except AttributeError:
            continue
        generic_setattr(wrapper, name, value)


@kernel_type(classmethod)
class classmethod:
    """Convert a function to be a class method."""

    def __new__(cls, /, *args, **kwargs):
        return alloc(cls, classmethod)

    def __init__(self, f):
        field_set(self, _CALLABLE, f)
        _wraps(self, f)

    def __get__(self, instance, owner=None):
        if owner is None:
            owner = type(instance)
        return method_new(field_get(self, _CALLABLE), owner)

    __isabstractmethod__ = getset(_wrapped_abstract)


@kernel_type(staticmethod)
class staticmethod:
    """Convert a function to be a static method."""

    def __new__(cls, /, *args, **kwargs):
        return alloc(cls, staticmethod)

    def __init__(self, f):
        field_set(self, _CALLABLE, f)
        _wraps(self, f)

    def __get__(self, instance, owner=None):
        return field_get(self, _CALLABLE)

    def __call__(self, *args, **kwargs):
        return field_get(self, _CALLABLE)(*args, **kwargs)

    __isabstractmethod__ = getset(_wrapped_abstract)


def _supercheck(cls, obj):
    """supercheck: the type the MRO is searched from."""
    if kind(obj, 'type') and is_subtype(obj, cls):
        return obj
    if is_subtype(type(obj), cls):
        return type(obj)
    c = generic_getattr(obj, '__class__', True)
    if kind(c, 'type') and c is not type(obj) and is_subtype(c, cls):
        return c
    error(TypeError, str_join('', (
        'super(type, obj): obj (instance of ', type_tp_name(type(obj)),
        ') is not an instance or subtype of type (', type_tp_name(cls), ').')))


def _super_lookup(cls, obj, starttype, name):
    """do_super_lookup: name in the MRO of starttype after cls, bound."""
    mro = type_mro(starttype)
    if mro is None:
        return MISSING
    after = False
    for t in mro:
        if after:
            res = ns_get(type_dict(t), name, MISSING)
            if res is not MISSING:
                get = type_lookup(type(res), '__get__')
                if get is not MISSING:
                    res = get(res, None if obj is starttype else obj, starttype)
                return res
        elif t is cls:
            after = True
    return MISSING


def super_getattr(cls, obj, name):
    """_PySuper_Lookup, for super().name in a method (LOAD_SUPER_ATTR)."""
    res = _super_lookup(cls, obj, _supercheck(cls, obj), name)
    if res is not MISSING:
        return res
    return generic_getattr(super(cls, obj), name)


@kernel_type(super)
class super:
    """super() -> same as super(__class__, <first argument>)
super(type) -> unbound super object
super(type, obj) -> bound super object; requires isinstance(obj, type)
super(type, type2) -> bound super object; requires issubclass(type2, type)"""

    def __new__(cls, /, *args, **kwargs):
        return alloc(cls, super)

    def __init__(self, cls=MISSING, obj=MISSING, /):
        if cls is MISSING:
            cls, obj = super_args()
        elif obj is MISSING:
            obj = None
        if not kind(cls, 'type'):
            error(TypeError, str_join('', (
                'super() argument 1 must be a type, not ', type_tp_name(type(cls)))))
        field_set(self, _THISCLASS, cls)
        field_set(self, _SELF, obj)
        field_set(self, _SELF_CLASS, None if obj is None else _supercheck(cls, obj))

    def __getattribute__(self, name):
        starttype = field_get(self, _SELF_CLASS)
        if starttype is not None and name != '__class__':
            res = _super_lookup(field_get(self, _THISCLASS), field_get(self, _SELF), starttype, name)
            if res is not MISSING:
                return res
        return generic_getattr(self, name)

    def __get__(self, instance, owner=None):
        if instance is None or field_get(self, _SELF) is not None:
            return self
        return type(self)(field_get(self, _THISCLASS), instance)

    def __repr__(self):
        cls = field_get(self, _THISCLASS)
        name = type_tp_name(cls) if cls is not None else 'NULL'
        start = field_get(self, _SELF_CLASS)
        if start is None:
            return str_join('', ('<super: <class ', _quote(name), '>, NULL>'))
        return str_join('', ('<super: <class ', _quote(name), '>, <', type_tp_name(start),
                             ' object>>'))


kernel_set('super_getattr', super_getattr)
