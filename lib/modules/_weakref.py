"""_weakref (Modules/_weakref.c, Objects/weakrefobject.c): weak references
to objects of heap types. ReferenceType is a kernel type over a native
list on the referent; the proxies are classes here, over a reference."""

import _lucifer

ref = ReferenceType = _lucifer.static_type('weakref.ReferenceType')


def getweakrefcount(object, /):
    """Return the number of weak references to 'object'."""
    return len(_lucifer.weakrefs_of(object))


def getweakrefs(object, /):
    """Return a list of all weak reference objects pointing to 'object'."""
    return _lucifer.weakrefs_of(object)


def _remove_dead_weakref(dct, key, /):
    """Atomically remove key from dict if it points to a dead weakref."""
    value = dct.get(key)
    if isinstance(value, ReferenceType) and value() is None:
        del dct[key]


def _dead(proxy_ref):
    raise ReferenceError('weakly-referenced object no longer exists')


def _referent(proxy):
    ob = object.__getattribute__(proxy, '_ref')()
    if ob is None:
        _dead(None)
    return ob


class ProxyType:
    """A proxy for an object that is weakly referenced."""

    __slots__ = ('_ref', '__weakref__')

    def __init__(self, ob, callback=None):
        object.__setattr__(self, '_ref', ReferenceType(ob, callback))

    def __getattr__(self, name):
        return getattr(_referent(self), name)

    def __setattr__(self, name, value):
        setattr(_referent(self), name, value)

    def __delattr__(self, name):
        delattr(_referent(self), name)

    def __repr__(self):
        ob = object.__getattribute__(self, '_ref')()
        if ob is None:
            return '<weakproxy at %#x; dead>' % (id(self),)
        return '<weakproxy at %#x; to %r at %#x>' % (id(self), type(ob).__qualname__, id(ob))

    def __str__(self):
        return str(_referent(self))

    def __bool__(self):
        return bool(_referent(self))

    def __hash__(self):
        raise TypeError("unhashable type: 'weakref.ProxyType'")

    def __len__(self):
        return len(_referent(self))

    def __iter__(self):
        return iter(_referent(self))

    def __next__(self):
        return next(_referent(self))

    def __getitem__(self, key):
        return _referent(self)[key]

    def __setitem__(self, key, value):
        _referent(self)[key] = value

    def __delitem__(self, key):
        del _referent(self)[key]

    def __contains__(self, item):
        return item in _referent(self)

    def __eq__(self, other):
        return _referent(self) == (_referent(other) if isinstance(other, ProxyType) else other)

    def __ne__(self, other):
        return _referent(self) != (_referent(other) if isinstance(other, ProxyType) else other)


class CallableProxyType(ProxyType):
    """A proxy for a callable object that is weakly referenced."""

    __slots__ = ()

    def __call__(self, *args, **kwargs):
        return _referent(self)(*args, **kwargs)


ProxyType.__module__ = CallableProxyType.__module__ = 'weakref'


def proxy(object, callback=None, /):
    """Create a proxy object that weakly references 'object'.

'callback', if given, is called with a reference to the
proxy when 'object' is about to be finalized."""
    return (CallableProxyType if callable(object) else ProxyType)(object, callback)
