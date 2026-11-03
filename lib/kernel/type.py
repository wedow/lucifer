"""type: class creation, the class attribute protocol, MRO and naming
(Objects/typeobject.c's type_new, type_call, type_getattro, type_setattro
and the type getsets over the layout primitives)."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_dict, type_mro, type_bases,
    type_base, type_name, type_qualname, type_set_name, type_tp_name, type_flag,
    type_can_new, type_new_layout, type_set_mro, type_set_bases, type_subclasses,
    type_modified, type_static_module, is_subtype, ns_get, ns_set, ns_del, ns_items, cell_set, proxy_new,
    field_get, kernel_get,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
getset = _boot.getset
is_data = _boot.is_data
call_special = _boot.call_special
update_slots = _boot.update_slots
mro_of = _boot.mro_of
calculate_metaclass = _boot.calculate_metaclass

generic_setattr = kernel_get('generic_setattr')

__all__ = ('type',)

_HEAPTYPE = 1 << 9
_staticmethod = staticmethod
_classmethod = classmethod


def _first(seq):
    for x in seq:
        return x


def _repr_str(s):
    return str_join('', ("'", s, "'"))


def _check_heap_set(cls, attr, value, what, noun):
    """check_set_special_type_attr and the setters' type checks."""
    if not type_flag(cls, _HEAPTYPE):
        error(TypeError, str_join('', (
            "cannot set '", attr, "' attribute of immutable type '", type_tp_name(cls), "'")))
    if value is MISSING:
        error(TypeError, str_join('', ("cannot delete '", attr, "' attribute of immutable type '",
                                       type_tp_name(cls), "'")))
    if not kind(value, what):
        error(TypeError, str_join('', (
            'can only assign ', noun, ' to ', type_tp_name(cls), '.', attr, ", not '",
            type_tp_name(type(value)), "'")))


def _get_name(cls):
    return type_name(cls)


def _set_name(cls, value):
    _check_heap_set(cls, '__name__', value, 'str', 'string')
    type_set_name(cls, 0, value)


def _get_qualname(cls):
    return type_qualname(cls)


def _set_qualname(cls, value):
    _check_heap_set(cls, '__qualname__', value, 'str', 'string')
    type_set_name(cls, 1, value)


def _get_module(cls):
    if type_flag(cls, _HEAPTYPE):
        module = ns_get(type_dict(cls), '__module__', MISSING)
        if module is MISSING:
            error(AttributeError, '__module__')
        return module
    return type_static_module(cls)


def _set_module(cls, value):
    if not type_flag(cls, _HEAPTYPE):
        error(TypeError, str_join('', (
            "cannot set '__module__' attribute of immutable type '", type_tp_name(cls), "'")))
    ns_set(type_dict(cls), '__module__', value)
    type_modified(cls)


def _get_doc(cls):
    doc = ns_get(type_dict(cls), '__doc__', None)
    if type_flag(cls, _HEAPTYPE):
        get = type_lookup(type(doc), '__get__')
        if get is not MISSING:
            return get(doc, None, cls)
    return doc


def _set_doc(cls, value):
    if not type_flag(cls, _HEAPTYPE):
        error(TypeError, str_join('', (
            "cannot set '__doc__' attribute of immutable type '", type_tp_name(cls), "'")))
    ns_set(type_dict(cls), '__doc__', None if value is MISSING else value)
    type_modified(cls)


def _get_dict(cls):
    return proxy_new(type_dict(cls))


def _get_bases(cls):
    return type_bases(cls)


def _set_bases(cls, value):
    _check_heap_set(cls, '__bases__', value, 'tuple', 'tuple')
    for base in value:
        if not kind(base, 'type'):
            error(TypeError, str_join('', (
                type_tp_name(cls), '.__bases__ must be tuple of classes, not ',
                _repr_str(type_tp_name(type(base))))))
        if base is cls or is_subtype(base, cls):
            error(TypeError, 'a __bases__ item causes an inheritance cycle')
    type_set_bases(cls, value)
    _remro(cls)


def _remro(cls):
    """mro_hierarchy: a new MRO for cls and its subclasses, and their slots."""
    type_set_mro(cls, _compute_mro(cls))
    update_slots(cls, False)
    for sub in type_subclasses(cls):
        _remro(sub)


def _get_mro(cls):
    return type_mro(cls)


def _get_base(cls):
    return type_base(cls)


def _compute_mro(cls):
    """mro_invoke: mro() of the metaclass when overridden, else C3."""
    meth = type_lookup(type(cls), 'mro')
    if meth is _type_mro:
        return mro_of(cls)
    result = call_special(meth, cls)
    return (*result,)


def _set_names(cls):
    for key, value in ns_items(type_dict(cls)):
        set_name = type_lookup(type(value), '__set_name__')
        if set_name is not MISSING:
            call_special(set_name, value, cls, key)


@kernel_type(type)
class type:
    """type(object) -> the object's type
type(name, bases, dict, **kwds) -> a new type"""

    def __call__(cls, /, *args, **kwargs):
        if cls is type:
            n = size(args)
            if n == 1 and not kwargs:
                return type(_first(args))
            if n != 3:
                error(TypeError, 'type() takes 1 or 3 arguments')
        if not type_can_new(cls):
            error(TypeError, str_join('', ("cannot create '", type_tp_name(cls), "' instances")))
        new = type_lookup(cls, '__new__')
        if type(new) is _staticmethod:
            new = field_get(new, 0)
        obj = new(cls, *args, **kwargs)
        if not is_subtype(type(obj), cls):
            return obj
        init = type_lookup(type(obj), '__init__')
        if init is not MISSING:
            res = call_special(init, obj, *args, **kwargs)
            if res is not None:
                error(TypeError, str_join('', (
                    "__init__() should return None, not '", type_tp_name(type(res)), "'")))
        return obj

    def __new__(metatype, *args, **kwargs):
        n = size(args)
        if metatype is type and n == 1 and not kwargs:
            return type(_first(args))
        if n != 3:
            error(TypeError, 'type() takes 1 or 3 arguments')
        name, bases, ns = args
        if not kind(name, 'str'):
            error(TypeError, str_join('', (
                'type.__new__() argument 1 must be str, not ', type_tp_name(type(name)))))
        if not kind(bases, 'tuple'):
            error(TypeError, str_join('', (
                'type.__new__() argument 2 must be tuple, not ', type_tp_name(type(bases)))))
        if not kind(ns, 'dict'):
            error(TypeError, str_join('', (
                'type.__new__() argument 3 must be dict, not ', type_tp_name(type(ns)))))
        if bases:
            for base in bases:
                if not kind(base, 'type') and type_lookup(type(base), '__mro_entries__') is not MISSING:
                    error(TypeError, "type() doesn't support MRO entry resolution; use types.new_class()")
            winner = calculate_metaclass(metatype, bases)
            if winner is not metatype:
                new = type_lookup(winner, '__new__')
                if type(new) is _staticmethod:
                    new = field_get(new, 0)
                if new is not _type_new:
                    return new(winner, *args, **kwargs)
                metatype = winner
        cls = type_new_layout(metatype, name, bases, ns)
        d = type_dict(cls)
        new = ns_get(d, '__new__', MISSING)
        if kind(new, 'function'):
            ns_set(d, '__new__', _staticmethod(new))
        for special in ('__init_subclass__', '__class_getitem__'):
            f = ns_get(d, special, MISSING)
            if kind(f, 'function'):
                ns_set(d, special, _classmethod(f))
        if ns_get(d, '__doc__', MISSING) is MISSING:
            ns_set(d, '__doc__', None)
        if ns_get(d, '__eq__', MISSING) is not MISSING and ns_get(d, '__hash__', MISSING) is MISSING:
            ns_set(d, '__hash__', None)
        for cellname, value in (('__classcell__', cls), ('__classdictcell__', d)):
            cell = ns_get(d, cellname, MISSING)
            if cell is not MISSING:
                if not kind(cell, 'cell'):
                    error(TypeError, str_join('', (
                        cellname, ' must be a nonlocal cell, not ', type.__repr__(type(cell)))))
                cell_set(cell, value)
                ns_del(d, cellname)
        type_set_mro(cls, _compute_mro(cls))
        update_slots(cls, False)
        _set_names(cls)
        init_subclass = super(cls, cls).__init_subclass__
        init_subclass(**kwargs)
        return cls

    def __init__(cls, *args, **kwargs):
        n = size(args)
        if n == 1 and kwargs:
            error(TypeError, 'type.__init__() takes no keyword arguments')
        if n != 1 and n != 3:
            error(TypeError, 'type.__init__() takes 1 or 3 arguments')

    def __getattribute__(cls, name):
        if not kind(name, 'str'):
            error(TypeError, str_join('', (
                "attribute name must be string, not '", type_tp_name(type(name)), "'")))
        meta = type(cls)
        meta_attr = type_lookup(meta, name)
        meta_get = MISSING
        if meta_attr is not MISSING:
            meta_get = type_lookup(type(meta_attr), '__get__')
            if meta_get is not MISSING and is_data(meta_attr):
                return meta_get(meta_attr, cls, meta)
        attr = type_lookup(cls, name)
        if attr is not MISSING:
            local_get = type_lookup(type(attr), '__get__')
            if local_get is MISSING:
                return attr
            return local_get(attr, None, cls)
        if meta_get is not MISSING:
            return meta_get(meta_attr, cls, meta)
        if meta_attr is not MISSING:
            return meta_attr
        error(AttributeError, str_join('', (
            "type object '", type_tp_name(cls), "' has no attribute '", name, "'")))

    def __setattr__(cls, name, value):
        _type_set(cls, name, value)

    def __delattr__(cls, name):
        _type_set(cls, name, MISSING)

    def __repr__(cls):
        module = cls.__module__
        if kind(module, 'str') and module != 'builtins':
            return str_join('', ("<class '", module, '.', type_qualname(cls), "'>"))
        return str_join('', ("<class '", type_tp_name(cls), "'>"))

    def mro(cls):
        """Return a type's method resolution order."""
        return [*mro_of(cls)]

    @classmethod_
    def __prepare__(mcls, name, bases, **kwargs):
        """Create the namespace for the class statement"""
        return {}

    def __subclasses__(cls):
        """Return a list of immediate subclasses."""
        return type_subclasses(cls)

    def __instancecheck__(cls, instance):
        """Check if an object is an instance."""
        if is_subtype(type(instance), cls):
            return True
        c = instance.__class__
        return c is not type(instance) and kind(c, 'type') and is_subtype(c, cls)

    def __subclasscheck__(cls, subclass):
        """Check if a class is a subclass."""
        if not kind(subclass, 'type'):
            error(TypeError, 'issubclass() arg 1 must be a class')
        return is_subtype(subclass, cls)

    __name__ = getset(_get_name, _set_name)
    __qualname__ = getset(_get_qualname, _set_qualname)
    __module__ = getset(_get_module, _set_module)
    __doc__ = getset(_get_doc, _set_doc)
    __dict__ = getset(_get_dict)
    __bases__ = getset(_get_bases, _set_bases)
    __mro__ = getset(_get_mro)
    __base__ = getset(_get_base)


def _type_set(cls, name, value):
    """type_setattro: immutable types refuse; a dunder updates the slots."""
    if not type_flag(cls, _HEAPTYPE):
        error(TypeError, str_join('', (
            "cannot set '", name, "' attribute of immutable type '", type_tp_name(cls), "'")))
    generic_setattr(cls, name, value)
    type_modified(cls)
    if ns_get(_boot.SLOT_DUNDERS, name, False):
        _remro_slots(cls)


def _remro_slots(cls):
    update_slots(cls, False)
    for sub in type_subclasses(cls):
        _remro_slots(sub)


_type_dict = type_dict(type)
_type_new = ns_get(_type_dict, '__new__', MISSING)
_type_mro = ns_get(_type_dict, 'mro', MISSING)
