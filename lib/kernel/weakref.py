"""weakref.ReferenceType (Objects/weakrefobject.c): the methods of the weak
reference type. The reference itself, its list on the referent and the
clearing at the referent's death are native (runtime/object/weakref.c)."""

from _lucifer import (
    error, str_join, static_type, type_tp_name, weakref_new, weakref_get, weakref_callback,
    weakref_hash, weakref_set_hash,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type
getset = _boot.getset

__all__ = ()


def _type_name(ob):
    """The fully qualified name of the referent's type, as %T prints it."""
    tp = type(ob)
    module = tp.__module__
    if module in ('builtins', '__main__'):
        return tp.__qualname__
    return str_join('', (module, '.', tp.__qualname__))


@kernel_type(static_type('weakref.ReferenceType'))
class ReferenceType:
    """Create a weak reference to 'object'.

When 'object' is deleted or finalized, 'callback' is called with the weak
reference as argument if it was given."""

    def __new__(cls, ob, callback=None, /):
        return weakref_new(cls, ob, callback)

    def __init__(self, ob, callback=None, /):
        pass

    def __call__(self):
        return weakref_get(self)

    def __hash__(self):
        h = weakref_hash(self)
        if h == -1:
            ob = weakref_get(self)
            if ob is None:
                error(TypeError, 'weak object has gone away')
            h = hash(ob)
            weakref_set_hash(self, h)
        return h

    def __eq__(self, other):
        if not isinstance(other, ReferenceType):
            return NotImplemented
        a = weakref_get(self)
        b = weakref_get(other)
        if a is None or b is None:
            return self is other
        return a == b

    def __ne__(self, other):
        if not isinstance(other, ReferenceType):
            return NotImplemented
        a = weakref_get(self)
        b = weakref_get(other)
        if a is None or b is None:
            return self is not other
        return a != b

    def __repr__(self):
        ob = weakref_get(self)
        if ob is None:
            return '<weakref at %#x; dead>' % (id(self),)
        try:
            name = ob.__name__
        except AttributeError:
            name = None
        text = "<weakref at %#x; to '%s' at %#x" % (id(self), _type_name(ob), id(ob))
        if isinstance(name, str):
            text = text + ' (' + name + ')'
        return text + '>'

    __callback__ = getset(lambda self: weakref_callback(self))
