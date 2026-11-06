"""_weakref (Modules/_weakref.c, Objects/weakrefobject.c): weak references
to objects of heap types. ReferenceType, ProxyType and CallableProxyType are
kernel types over a native list on the referent."""

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


ProxyType = _lucifer.static_type('weakref.ProxyType')
CallableProxyType = _lucifer.static_type('weakref.CallableProxyType')


def proxy(object, callback=None, /):
    """Create a proxy object that weakly references 'object'.

'callback', if given, is called with a reference to the
proxy when 'object' is about to be finalized."""
    return _lucifer.weakref_new(CallableProxyType if callable(object) else ProxyType, object, callback)
