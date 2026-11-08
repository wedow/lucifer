"""type: class creation, the class attribute protocol, MRO and naming
(Objects/typeobject.c's type_new, type_call, type_getattro, type_setattro
and the type getsets over the layout primitives)."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_dict, type_mro, type_bases,
    type_base, type_name, type_qualname, type_set_name, type_tp_name, type_text_signature, type_flag, type_set_flag,
    type_can_new, type_new_layout, type_set_mro, type_set_bases, type_subclasses,
    type_modified, type_static_module, is_subtype, ns_get, ns_set, ns_del, ns_items, cell_set, proxy_new,
    field_get, kernel_get, is_callable, type_layout, enable_deferred, instance_store,
)
from _lucifer import _boot, object as _object

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
_function = _boot._function
getset = _boot.getset
is_data = _boot.is_data
call_special = _boot.call_special
update_slots = _boot.update_slots
mro_of = _boot.mro_of
calculate_metaclass = _boot.calculate_metaclass

generic_setattr = kernel_get('generic_setattr')
merge_class_dict = _object.merge_class_dict

__all__ = ('type',)

_HEAPTYPE = 1 << 9
_IMMUTABLETYPE = 1 << 8      # setattr refuses (check_set_special_type_attr, type_setattro)
_staticmethod = staticmethod
_classmethod = classmethod


def _first(seq):
    for x in seq:
        return x


def _repr_str(s):
    return str_join('', ("'", s, "'"))


def _check_heap_set(cls, attr, value, what, noun):
    """check_set_special_type_attr and the setters' type checks."""
    if type_flag(cls, _IMMUTABLETYPE):
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
    if type_flag(cls, _IMMUTABLETYPE):
        error(TypeError, str_join('', (
            "cannot set '__module__' attribute of immutable type '", type_tp_name(cls), "'")))
    instance_store(cls, '__module__', value)


def _get_doc(cls):
    doc = ns_get(type_dict(cls), '__doc__', None)
    if type_flag(cls, _HEAPTYPE):
        get = type_lookup(type(doc), '__get__')
        if get is not MISSING:
            return get(doc, None, cls)
    return doc


def _set_doc(cls, value):
    if type_flag(cls, _IMMUTABLETYPE):
        error(TypeError, str_join('', (
            "cannot set '__doc__' attribute of immutable type '", type_tp_name(cls), "'")))
    instance_store(cls, '__doc__', None if value is MISSING else value)


_IS_ABSTRACT = 1 << 20


def _get_abstractmethods(cls):
    """type_abstractmethods: only the class's own, never type's."""
    value = ns_get(type_dict(cls), '__abstractmethods__', MISSING) if cls is not type else MISSING
    if value is MISSING:
        error(AttributeError, '__abstractmethods__')
    return value


def _set_abstractmethods(cls, value):
    """type_set_abstractmethods: Py_TPFLAGS_IS_ABSTRACT follows the truth of the value."""
    if value is MISSING:
        if not instance_store(cls, '__abstractmethods__', MISSING):
            error(AttributeError, '__abstractmethods__')
        abstract = False
    else:
        abstract = True if value else False
        instance_store(cls, '__abstractmethods__', value)
    type_set_flag(cls, _IS_ABSTRACT, abstract)


def _get_flags(cls):
    return type_layout(cls)[0]


def _get_basicsize(cls):
    return type_layout(cls)[1]


def _get_itemsize(cls):
    return type_layout(cls)[2]


def _get_dictoffset(cls):
    return type_layout(cls)[3]


def _get_weakrefoffset(cls):
    return type_layout(cls)[4]


def _get_text_signature(cls):
    return type_text_signature(cls)


def _get_type_params(cls):
    return ns_get(type_dict(cls), '__type_params__', ())


def _set_type_params(cls, value):
    if type_flag(cls, _IMMUTABLETYPE):
        error(TypeError, str_join('', (
            "cannot set '__type_params__' attribute of immutable type '", type_tp_name(cls), "'")))
    if not kind(value, 'tuple'):
        error(TypeError, str_join('', ('__type_params__ must be set to a tuple, not ', type(value).__name__)))
    instance_store(cls, '__type_params__', value)


def _heap_only(cls, name):
    if not type_flag(cls, _HEAPTYPE):
        error(AttributeError, str_join('', (
            "type object '", type_tp_name(cls), "' has no attribute '", name, "'")))


def _own(d, name):
    return ns_get(d, name, MISSING)


def _drop(cls, name):
    instance_store(cls, name, MISSING)


def _get_annotate(cls):
    """type_get_annotate."""
    _heap_only(cls, '__annotate__')
    d = type_dict(cls)
    annotate = _own(d, '__annotate__')
    if annotate is MISSING:
        annotate = _own(d, '__annotate_func__')
    if annotate is MISSING:
        ns_set(d, '__annotate_func__', None)
        return None
    get = type_lookup(type(annotate), '__get__')
    if get is not MISSING:
        annotate = get(annotate, None, cls)
    return annotate


def _set_annotate(cls, value):
    if value is MISSING:
        error(TypeError, 'cannot delete __annotate__ attribute')
    if type_flag(cls, _IMMUTABLETYPE):
        error(TypeError, str_join('', (
            "cannot set '__annotate__' attribute of immutable type '", type_tp_name(cls), "'")))
    if value is not None and not is_callable(value):
        error(TypeError, '__annotate__ must be callable or None')
    instance_store(cls, '__annotate_func__', value)
    if value is not None:
        _drop(cls, '__annotations_cache__')


def _get_annotations(cls):
    """type_get_annotations: the class's own, built from __annotate__ on first use."""
    _heap_only(cls, '__annotations__')
    d = type_dict(cls)
    annotations = _own(d, '__annotations__')
    if annotations is MISSING:
        annotations = _own(d, '__annotations_cache__')
    if annotations is not MISSING:
        get = type_lookup(type(annotations), '__get__')
        if get is not MISSING:
            annotations = get(annotations, None, cls)
        return annotations
    annotate = cls.__annotate__
    if is_callable(annotate):
        annotations = annotate(1)
        if not kind(annotations, 'dict'):
            error(TypeError, str_join('', (
                "__annotate__ returned non-dict of type '", type_tp_name(type(annotations)), "'")))
    else:
        annotations = {}
    instance_store(cls, '__annotations_cache__', annotations)
    return annotations


def _set_annotations(cls, value):
    if type_flag(cls, _IMMUTABLETYPE):
        error(TypeError, str_join('', (
            "cannot set '__annotations__' attribute of immutable type '", type_tp_name(cls), "'")))
    d = type_dict(cls)
    key = '__annotations__' if _own(d, '__annotations__') is not MISSING else '__annotations_cache__'
    if value is MISSING:
        if not instance_store(cls, key, MISSING):
            error(AttributeError, '__annotations__')
    else:
        instance_store(cls, key, value)
    if key == '__annotations__':
        _drop(cls, '__annotations_cache__')
    _drop(cls, '__annotate_func__')
    _drop(cls, '__annotate__')


def _get_dict(cls):
    return proxy_new(type_dict(cls))


def _get_bases(cls):
    return type_bases(cls)


def _base_chain_has(base, cls):
    """Whether cls is on base's tp_base chain, which a reentrant mro() may have
    changed before the MRO caught up."""
    while base is not None:
        if base is cls:
            return True
        base = type_base(base)
    return False


def _set_bases(cls, value):
    _check_heap_set(cls, '__bases__', value, 'tuple', 'tuple')
    for base in value:
        if not kind(base, 'type'):
            error(TypeError, str_join('', (
                type_tp_name(cls), '.__bases__ must be tuple of classes, not ',
                _repr_str(type_tp_name(type(base))))))
        if is_subtype(base, cls) or _base_chain_has(base, cls):
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


def _type_getattr(cls, name, suppress=False):
    """type_getattro; MISSING instead of AttributeError when suppress."""
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
    if suppress:
        return MISSING
    raise AttributeError(str_join('', (
        "type object '", type_tp_name(cls), "' has no attribute '", name, "'")), name=name, obj=cls)


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
        # object.__init__ refuses arguments only when __new__ is object's too.
        if init is _object._object_init and (args or kwargs) and type_lookup(type(obj), '__new__') is not _object._object_new:
            return obj
        if init is not MISSING and (init is not _object._object_init or args or kwargs):
            if type(init) is _function:
                res = init(obj, *args, **kwargs)
            else:
                res = call_special(init, obj, *args, **kwargs)
            if res is not None:
                error(TypeError, str_join('', (
                    "__init__() should return None, not '", type_tp_name(type(res)), "'")))
        return obj

    def __new__(metatype, /, *args, **kwargs):
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
                if not kind(base, 'type') and hasattr(base, '__mro_entries__'):
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
        return _type_getattr(cls, name)

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

    def __dir__(cls):
        """Specialized __dir__ implementation for types."""
        d = {}
        merge_class_dict(d, cls)
        return list(d)

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
    __annotations__ = getset(_get_annotations, _set_annotations)
    __annotate__ = getset(_get_annotate, _set_annotate)
    __abstractmethods__ = getset(_get_abstractmethods, _set_abstractmethods)
    __bases__ = getset(_get_bases, _set_bases)
    __mro__ = getset(_get_mro)
    __flags__ = getset(_get_flags)
    __basicsize__ = getset(_get_basicsize)
    __itemsize__ = getset(_get_itemsize)
    __dictoffset__ = getset(_get_dictoffset)
    __weakrefoffset__ = getset(_get_weakrefoffset)
    __text_signature__ = getset(_get_text_signature)
    __type_params__ = getset(_get_type_params, _set_type_params)
    __base__ = getset(_get_base)


def _type_set(cls, name, value):
    """type_setattro: immutable types refuse; a dunder updates the slots.
    A store into the type's dict takes back the cached lookups before it
    releases the value it replaced (instance_store); the calls here are for
    what a descriptor's setter changes."""
    if type_flag(cls, _IMMUTABLETYPE):
        error(TypeError, str_join('', (
            "cannot set '", name, "' attribute of immutable type '", type_tp_name(cls), "'")))
    if type(value) is _function:
        enable_deferred(value)          # cp314t (gh-139103): a class's methods are freed by collections
    type_modified(cls)
    generic_setattr(cls, name, value)
    type_modified(cls)
    if ns_get(_boot.SLOT_DUNDERS, name, False):
        _remro_slots(cls)


def _remro_slots(cls):
    update_slots(cls, False)
    for sub in type_subclasses(cls):
        _remro_slots(sub)


_type_dict = type_dict(type)
_object.type_getattribute = ns_get(_type_dict, '__getattribute__', MISSING)
_object.type_getattr = _type_getattr
_type_new = ns_get(_type_dict, '__new__', MISSING)
_type_mro = ns_get(_type_dict, 'mro', MISSING)
