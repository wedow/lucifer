"""object, and the attribute protocol every type's slots run (DESIGN 4.4).

getattr is slot_tp_getattr_hook: the type's __getattribute__, then its
__getattr__. generic_getattr and generic_setattr are PyObject_GenericGetAttr
and PyObject_GenericSetAttr: data descriptors on the type, then the instance
dict, then non-data descriptors and plain class attributes.
"""

from _lucifer import (
    MISSING, error, str_join, kind, type_lookup, type_dict, type_bases, type_tp_name, type_qualname,
    instance_dict, ns_get, ns_set, ns_del, alloc, set_class, kernel_set, address,
    object_hash, type_flag,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
getset = _boot.getset
is_data = _boot.is_data
call_special = _boot.call_special

__all__ = ('object',)


_IS_ABSTRACT = 1 << 20          # Py_TPFLAGS_IS_ABSTRACT


def _abstract_error(cls):
    """object_new's refusal of a class with abstract methods."""
    names = sorted(type_dict(cls)['__abstractmethods__'])
    error(TypeError, str_join('', (
        "Can't instantiate abstract class ", type_tp_name(cls),
        ' without an implementation for abstract method', 's' if len(names) > 1 else '', " '",
        str_join("', '", (*names,)), "'")))


def _no_attribute(obj, name):
    error(AttributeError, str_join('', (
        "'", type_tp_name(type(obj)), "' object has no attribute '", name, "'")))


def _check_name(name):
    if not kind(name, 'str'):
        error(TypeError, str_join('', (
            "attribute name must be string, not '", type_tp_name(type(name)), "'")))


def generic_getattr(obj, name, suppress=False):
    """_PyObject_GenericGetAttrWithDict; MISSING instead of AttributeError
    when suppress."""
    _check_name(name)
    tp = type(obj)
    descr = type_lookup(tp, name)
    get = MISSING
    if descr is not MISSING:
        get = type_lookup(type(descr), '__get__')
        if get is not MISSING and is_data(descr):
            return get(descr, obj, tp)
    d = instance_dict(obj, False)
    if d is not None:
        v = ns_get(d, name, MISSING)
        if v is not MISSING:
            return v
    if get is not MISSING:
        return get(descr, obj, tp)
    if descr is not MISSING:
        return descr
    if suppress:
        return MISSING
    _no_attribute(obj, name)


def generic_setattr(obj, name, value):
    """_PyObject_GenericSetAttrWithDict; value MISSING deletes."""
    _check_name(name)
    tp = type(obj)
    descr = type_lookup(tp, name)
    if descr is not MISSING:
        t = type(descr)
        setter = type_lookup(t, '__set__')
        deleter = type_lookup(t, '__delete__')
        if value is MISSING:
            if deleter is not MISSING:
                deleter(descr, obj)
                return None
            if setter is not MISSING:
                error(AttributeError, '__delete__')
        else:
            if setter is not MISSING:
                setter(descr, obj, value)
                return None
            if deleter is not MISSING:
                error(AttributeError, '__set__')
    d = instance_dict(obj, True)
    if d is None:
        if descr is not MISSING:
            error(AttributeError, str_join('', (
                "'", type_tp_name(tp), "' object attribute '", name, "' is read-only")))
        if type_lookup(tp, '__setattr__') is _object_setattr and value is not MISSING:
            error(AttributeError, str_join('', (
                "'", type_tp_name(tp), "' object has no attribute '", name,
                "' and no __dict__ for setting new attributes")))
        _no_attribute(obj, name)
    if value is MISSING:
        if not ns_del(d, name):
            _no_attribute(obj, name)
    else:
        ns_set(d, name, value)
    return None


def getattr_hook(obj, name):
    """PyObject_GetAttr for every kernel type and class."""
    tp = type(obj)
    getattribute = type_lookup(tp, '__getattribute__')
    getattr_ = type_lookup(tp, '__getattr__')
    if getattribute is _object_getattribute:
        if getattr_ is MISSING:
            return generic_getattr(obj, name)
        v = generic_getattr(obj, name, True)
        if v is not MISSING:
            return v
    elif getattr_ is MISSING:
        return call_special(getattribute, obj, name)
    else:
        try:
            return call_special(getattribute, obj, name)
        except AttributeError:
            pass
    return call_special(getattr_, obj, name)


def setattr_hook(obj, name, value):
    call_special(type_lookup(type(obj), '__setattr__'), obj, name, value)


def delattr_hook(obj, name):
    call_special(type_lookup(type(obj), '__delattr__'), obj, name)


def merge_class_dict(d, cls):
    """merge_class_dict of Objects/typeobject.c: the class's dict, then those
    of its bases, into d."""
    d.update(type_dict(cls))
    for base in type_bases(cls):
        merge_class_dict(d, base)


def _get_class(self):
    return type(self)


def _set_class(self, value):
    if value is MISSING:
        error(TypeError, "can't delete __class__ attribute")
    if not kind(value, 'type'):
        error(TypeError, str_join('', (
            "__class__ must be set to a class, not '", type_tp_name(type(value)), "' object")))
    set_class(self, value)


@kernel_type(object)
class object:
    """The base class of the class hierarchy.

When called, it accepts no arguments and returns a new featureless
instance that has no instance attributes and cannot be given any.
"""

    def __getattribute__(self, name):
        return generic_getattr(self, name)

    def __setattr__(self, name, value):
        generic_setattr(self, name, value)

    def __delattr__(self, name):
        generic_setattr(self, name, MISSING)

    def __new__(cls, *args, **kwargs):
        if not kind(cls, 'type'):
            error(TypeError, str_join('', (
                'object.__new__(X): X is not a type object (', type_tp_name(type(cls)), ')')))
        if args or kwargs:
            if type_lookup(cls, '__new__') is not _object_new:
                error(TypeError, 'object.__new__() takes exactly one argument (the type to instantiate)')
            if type_lookup(cls, '__init__') is _object_init:
                error(TypeError, str_join('', (type_tp_name(cls), '() takes no arguments')))
        if type_flag(cls, _IS_ABSTRACT):
            _abstract_error(cls)
        return alloc(cls, object)

    def __init__(self, *args, **kwargs):
        if args or kwargs:
            tp = type(self)
            if type_lookup(tp, '__init__') is not _object_init:
                error(TypeError, 'object.__init__() takes exactly one argument (the instance to initialize)')
            if type_lookup(tp, '__new__') is _object_new:
                error(TypeError, str_join('', (
                    type_tp_name(tp), '.__init__() takes exactly one argument (the instance to initialize)')))

    def __dir__(self):
        """Default dir() implementation."""
        try:
            d = getattr_hook(self, '__dict__')
        except AttributeError:
            d = None
        d = dict(d) if kind(d, 'dict') else {}
        try:
            cls = getattr_hook(self, '__class__')
        except AttributeError:
            cls = MISSING
        if cls is not MISSING:
            merge_class_dict(d, cls)
        return list(d)

    def __repr__(self):
        tp = type(self)
        module = tp.__module__
        if kind(module, 'str') and module != 'builtins':
            return str_join('', ('<', module, '.', type_qualname(tp), ' object at ', address(self), '>'))
        return str_join('', ('<', type_tp_name(tp), ' object at ', address(self), '>'))

    def __str__(self):
        return call_special(type_lookup(type(self), '__repr__'), self)

    def __hash__(self):
        return object_hash(self)

    def __eq__(self, other):
        return True if self is other else NotImplemented

    def __ne__(self, other):
        r = call_special(type_lookup(type(self), '__eq__'), self, other)
        if r is NotImplemented:
            return r
        return not r

    def __lt__(self, other):
        return NotImplemented

    def __le__(self, other):
        return NotImplemented

    def __gt__(self, other):
        return NotImplemented

    def __ge__(self, other):
        return NotImplemented

    def __format__(self, format_spec, /):
        """Default object formatter.

Return str(self) if format_spec is empty. Raise TypeError otherwise."""
        if not kind(format_spec, 'str'):
            error(TypeError, str_join('', ('__format__() argument must be str, not ',
                                           type_tp_name(type(format_spec)))))
        if format_spec:
            error(TypeError, str_join('', ('unsupported format string passed to ',
                                           type_tp_name(type(self)), '.__format__')))
        return str(self)

    def __getstate__(self):
        """Helper for pickle."""
        try:
            state = getattr_hook(self, '__dict__')
        except AttributeError:
            state = None
        if not state:
            state = None
        import copyreg
        slots = {}
        for name in copyreg._slotnames(type(self)):
            try:
                slots[name] = getattr_hook(self, name)
            except AttributeError:
                pass
        return (state, slots) if slots else state

    def __reduce__(self):
        """Helper for pickle."""
        import copyreg
        return copyreg._reduce_ex(self, 0)

    def __reduce_ex__(self, protocol, /):
        """Helper for pickle."""
        reduce = type_lookup(type(self), '__reduce__')
        if reduce is not _object_reduce:
            return call_special(reduce, self)
        import copyreg
        if protocol < 2:
            return copyreg._reduce_ex(self, protocol)
        cls = type(self)
        try:
            getnewargs_ex = getattr_hook(self, '__getnewargs_ex__')
        except AttributeError:
            getnewargs_ex = None
        if getnewargs_ex is not None:
            args, kwargs = getnewargs_ex()
        else:
            kwargs = None
            try:
                getnewargs = getattr_hook(self, '__getnewargs__')
            except AttributeError:
                args = ()
            else:
                args = getnewargs()
        if kwargs:
            newobj, newargs = copyreg.__newobj_ex__, (cls, args, kwargs)
        else:
            newobj, newargs = copyreg.__newobj__, (cls, *args)
        listitems = iter(self) if isinstance(self, list) else None
        dictitems = iter(self.items()) if isinstance(self, dict) else None
        return newobj, newargs, getattr_hook(self, '__getstate__')(), listitems, dictitems

    @classmethod_
    def __init_subclass__(cls):
        """This method is called when a class is subclassed.

The default implementation does nothing. It may be
overridden to extend subclasses.
"""
        return None

    @classmethod_
    def __subclasshook__(cls, *args):
        return NotImplemented

    __class__ = getset(_get_class, _set_class)


_object_dict = type_dict(object)
_object_new = ns_get(_object_dict, '__new__', MISSING)
_object_init = ns_get(_object_dict, '__init__', MISSING)
_object_reduce = ns_get(_object_dict, '__reduce__', MISSING)
_object_getattribute = ns_get(_object_dict, '__getattribute__', MISSING)
_object_setattr = ns_get(_object_dict, '__setattr__', MISSING)

kernel_set('getattr', getattr_hook)
kernel_set('generic_getattr', generic_getattr)
kernel_set('setattr', setattr_hook)
kernel_set('delattr', delattr_hook)
kernel_set('generic_setattr', generic_setattr)
