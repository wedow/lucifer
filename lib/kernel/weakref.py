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


def _unpack(name, args, kwargs):
    """parse_weakref_init_args: the object and the optional callback."""
    if kwargs:
        error(TypeError, str_join('', (name, ' takes no keyword arguments')))
    n = len(args)
    if n < 1:
        error(TypeError, str_join('', (name, ' expected at least 1 argument, got ', str(n))))
    if n > 2:
        error(TypeError, str_join('', (name, ' expected at most 2 arguments, got ', str(n))))


@kernel_type(static_type('weakref.ReferenceType'))
class ReferenceType:
    """Create a weak reference to 'object'.

When 'object' is deleted or finalized, 'callback' is called with the weak
reference as argument if it was given."""

    def __new__(cls, /, *args, **kwargs):
        _unpack('__new__', args, kwargs)
        return weakref_new(cls, args[0], args[1] if len(args) > 1 else None)

    def __init__(self, /, *args, **kwargs):
        _unpack('__init__', args, kwargs)

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


# The proxies (Objects/weakrefobject.c proxy_*): weak references of their
# own static types, ProxyType and, for a callable referent, CallableProxyType,
# whose every operation forwards to the referent, unwrapping proxy operands.
from _lucifer import operators

_proxy_type = static_type('weakref.ProxyType')
_callable_proxy_type = static_type('weakref.CallableProxyType')


def _unwrap(x):
    """UNWRAP: a proxy's referent, a ReferenceError once it is dead."""
    t = type(x)
    if t is _proxy_type or t is _callable_proxy_type:
        ob = weakref_get(x)
        if ob is None:
            error(ReferenceError, 'weakly-referenced object no longer exists')
        return ob
    return x


def _binary(i):
    return lambda self, other: operators.binary_op(_unwrap(self), _unwrap(other), i)


def _reflected(i):
    return lambda self, other: operators.binary_op(_unwrap(other), _unwrap(self), i)


def _p_repr(self):
    ob = weakref_get(self)
    if ob is None:
        return '<weakproxy at %#x; dead>' % (id(self),)
    return "<weakproxy at %#x; to '%s' at %#x>" % (id(self), _type_name(ob), id(ob))


def _p_next(self):
    ob = _unwrap(self)
    if not hasattr(type(ob), '__next__'):
        error(TypeError, str_join('', ("Weakref proxy referenced a non-iterator '", type_tp_name(type(ob)),
                                       "' object")))
    return next(ob)


def _p_hash(self):
    error(TypeError, str_join('', ("unhashable type: '", type_tp_name(type(self)), "'")))


_PROXY = {
    '__getattribute__': lambda self, name: getattr(_unwrap(self), name),
    '__setattr__': lambda self, name, value: setattr(_unwrap(self), name, value),
    '__delattr__': lambda self, name: delattr(_unwrap(self), name),
    '__repr__': _p_repr,
    '__str__': lambda self: str(_unwrap(self)),
    '__bytes__': lambda self: bytes(_unwrap(self)),
    '__reversed__': lambda self: reversed(_unwrap(self)),
    '__hash__': _p_hash,
    '__bool__': lambda self: bool(_unwrap(self)),
    '__len__': lambda self: len(_unwrap(self)),
    '__iter__': lambda self: iter(_unwrap(self)),
    '__next__': _p_next,
    '__getitem__': lambda self, key: _unwrap(self)[_unwrap(key)],
    '__setitem__': lambda self, key, value: operators_setitem(self, key, value),
    '__delitem__': lambda self, key: operators_delitem(self, key),
    '__contains__': lambda self, item: _unwrap(item) in _unwrap(self),
    '__lt__': lambda self, other: _unwrap(self) < _unwrap(other),
    '__le__': lambda self, other: _unwrap(self) <= _unwrap(other),
    '__eq__': lambda self, other: _unwrap(self) == _unwrap(other),
    '__ne__': lambda self, other: _unwrap(self) != _unwrap(other),
    '__gt__': lambda self, other: _unwrap(self) > _unwrap(other),
    '__ge__': lambda self, other: _unwrap(self) >= _unwrap(other),
    '__neg__': lambda self: -_unwrap(self),
    '__pos__': lambda self: +_unwrap(self),
    '__abs__': lambda self: abs(_unwrap(self)),
    '__invert__': lambda self: ~_unwrap(self),
    '__int__': lambda self: int(_unwrap(self)),
    '__float__': lambda self: float(_unwrap(self)),
    '__index__': lambda self: operators.index(_unwrap(self)),
}


def operators_setitem(self, key, value):
    _unwrap(self)[_unwrap(key)] = _unwrap(value)


def operators_delitem(self, key):
    del _unwrap(self)[_unwrap(key)]


for _i, (_name, _rname, _iname, _sym) in enumerate(operators._BINARY):
    _PROXY[_name] = _binary(_i)
    _PROXY[_rname] = _reflected(_i)
    _PROXY[_iname] = _binary(13 + _i)
_PROXY['__pow__'] = lambda self, other, mod=None: pow(_unwrap(self), _unwrap(other), _unwrap(mod))
_PROXY['__rpow__'] = lambda self, other, mod=None: pow(_unwrap(other), _unwrap(self), _unwrap(mod))

kernel_type(_proxy_type)(('ProxyType', (), dict(_PROXY)))
kernel_type(_callable_proxy_type)(('CallableProxyType', (), dict(
    _PROXY, __call__=lambda self, /, *args, **kwargs: _unwrap(self)(*args, **kwargs))))
