"""builtins functions, over the _lucifer primitives."""

from _lucifer import write, str_join, error, kind, type_lookup, MISSING
from _lucifer import _boot

call_special = _boot.call_special

__all__ = ('print', 'isinstance', 'issubclass')


def print(*args, sep=' ', end='\n'):
    write(1, str_join(sep, args))
    write(1, end)


def _check(name, cls, inst, method, what):
    """PyObject_IsInstance and PyObject_IsSubclass: exact type, tuples of
    classes, then the metaclass's __instancecheck__ or __subclasscheck__."""
    if kind(cls, 'tuple'):
        for c in cls:
            if _check(name, c, inst, method, what):
                return True
        return False
    check = type_lookup(type(cls), method)
    if check is MISSING:
        error(TypeError, str_join('', (name, '() arg 2 must be a ', what)))
    return True if call_special(check, cls, inst) else False


def isinstance(obj, class_or_tuple, /):
    """Return whether an object is an instance of a class or of a subclass thereof."""
    if type(obj) is class_or_tuple:
        return True
    return _check('isinstance', class_or_tuple, obj, '__instancecheck__',
                  'type, a tuple of types, or a union')


def issubclass(cls, class_or_tuple, /):
    """Return whether 'cls' is derived from another class or is the same class."""
    return _check('issubclass', class_or_tuple, cls, '__subclasscheck__',
                  'class, a tuple of classes, or a union')
