"""Module contains faster C implementation of abc.ABCMeta"""
# Modules/_abc.c as a builtin module. Its frames are elided as C frames are, so
# typing's protocol checks see abc.ABCMeta as the caller of __subclasscheck__.
# Py_TPFLAGS_SEQUENCE and _MAPPING are not set here: pattern matching asks the
# collections.abc classes instead (lib/kernel/match.py).

from _weakref import ref as _ref

_invalidation_counter = 0


class _abc_data:
    """Internal state held by ABC machinery."""

    __slots__ = ('_abc_registry', '_abc_cache', '_abc_negative_cache', '_abc_negative_cache_version')

    def __init__(self):
        self._abc_registry = self._abc_cache = self._abc_negative_cache = None
        self._abc_negative_cache_version = _invalidation_counter


def _get_impl(cls):
    impl = cls._abc_impl
    if type(impl) is not _abc_data:
        raise TypeError('_abc_impl is set to a wrong type')
    return impl


def _in_weak_set(weak_set, obj):
    if not weak_set:
        return False
    try:
        return _ref(obj) in weak_set
    except TypeError:
        return False


def _add_to_weak_set(impl, name, obj):
    weak_set = getattr(impl, name)
    if weak_set is None:
        weak_set = set()
        setattr(impl, name, weak_set)
    set_ref = _ref(weak_set)

    def _destroy(key):
        live = set_ref()
        if live is not None:
            live.discard(key)
    weak_set.add(_ref(obj, _destroy))


def _is_abstract(value):
    return bool(getattr(value, '__isabstractmethod__', False))


def _abc_init(self, /):
    """Internal ABC helper for class set-up. Should be never used outside abc module."""
    abstracts = {name for name, value in list(self.__dict__.items()) if _is_abstract(value)}
    bases = self.__bases__
    if type(bases) is not tuple:
        raise TypeError('__bases__ is not tuple')
    for base in bases:
        for name in getattr(base, '__abstractmethods__', ()):
            value = getattr(self, name, None)
            if value is not None and _is_abstract(value):
                abstracts.add(name)
    self.__abstractmethods__ = frozenset(abstracts)
    self._abc_impl = _abc_data()
    if isinstance(self, type) and '__abc_tpflags__' in self.__dict__:
        flags = self.__dict__['__abc_tpflags__']
        type.__delattr__(self, '__abc_tpflags__')
        if type(flags) is int and flags & (1 << 5) and flags & (1 << 6):
            raise TypeError('__abc_tpflags__ cannot be both Py_TPFLAGS_SEQUENCE and Py_TPFLAGS_MAPPING')


def _abc_register(self, subclass, /):
    """Internal ABC helper for subclasss registration. Should be never used outside abc module."""
    global _invalidation_counter
    if not isinstance(subclass, type):
        raise TypeError('Can only register classes')
    if issubclass(subclass, self):
        return subclass         # already a subclass
    # Subtle: test for cycles *after* testing for "already a subclass";
    # this means we allow X.register(X) and interpret it as a no-op.
    if issubclass(self, subclass):
        raise RuntimeError('Refusing to create an inheritance cycle')
    _add_to_weak_set(_get_impl(self), '_abc_registry', subclass)
    _invalidation_counter += 1  # invalidates the negative caches
    return subclass


def _abc_instancecheck(self, instance, /):
    """Internal ABC helper for instance checks. Should be never used outside abc module."""
    impl = _get_impl(self)
    subclass = getattr(instance, '__class__', None)
    if subclass is None:
        subclass = type(instance)
    if _in_weak_set(impl._abc_cache, subclass):
        return True
    subtype = type(instance)
    if subtype is subclass:
        if (impl._abc_negative_cache_version == _invalidation_counter
                and _in_weak_set(impl._abc_negative_cache, subclass)):
            return False
        return self.__subclasscheck__(subclass)
    return self.__subclasscheck__(subclass) or self.__subclasscheck__(subtype)


def _abc_subclasscheck(self, subclass, /):
    """Internal ABC helper for subclasss checks. Should be never used outside abc module."""
    if not isinstance(subclass, type):
        raise TypeError('issubclass() arg 1 must be a class')
    impl = _get_impl(self)
    # 1. The cache.
    if _in_weak_set(impl._abc_cache, subclass):
        return True
    # 2. The negative cache, which may have to be invalidated.
    if impl._abc_negative_cache_version < _invalidation_counter:
        if impl._abc_negative_cache is not None:
            impl._abc_negative_cache.clear()
        impl._abc_negative_cache_version = _invalidation_counter
    elif _in_weak_set(impl._abc_negative_cache, subclass):
        return False
    # 3. The subclass hook.
    ok = self.__subclasshook__(subclass)
    if ok is True:
        _add_to_weak_set(impl, '_abc_cache', subclass)
        return True
    if ok is False:
        _add_to_weak_set(impl, '_abc_negative_cache', subclass)
        return False
    if ok is not NotImplemented:
        raise AssertionError('__subclasshook__ must return either False, True, or NotImplemented')
    # 4. A direct subclass.
    if type.__subclasscheck__(self, subclass):
        _add_to_weak_set(impl, '_abc_cache', subclass)
        return True
    # 5. A subclass of a registered class (recursive).
    registry = impl._abc_registry
    if registry:
        if _in_weak_set(registry, subclass):
            return True
        for rcls in frozenset(registry):
            rcls = rcls()
            if rcls is not None and issubclass(subclass, rcls):
                _add_to_weak_set(impl, '_abc_cache', subclass)
                return True
    # 6. A subclass of a subclass (recursive).
    subclasses = self.__subclasses__()
    if type(subclasses) is not list:
        raise TypeError('__subclasses__() must return a list')
    for scls in subclasses:
        if issubclass(subclass, scls):
            _add_to_weak_set(impl, '_abc_cache', subclass)
            return True
    # No dice; update the negative cache.
    _add_to_weak_set(impl, '_abc_negative_cache', subclass)
    return False


def _get_dump(self, /):
    """Internal ABC helper for cache and registry debugging.

Return shallow copies of registry, of both caches, and
negative cache version. Don't call this function directly,
instead use ABC._dump_registry() for a nice repr."""
    impl = _get_impl(self)
    return (set(impl._abc_registry or ()), set(impl._abc_cache or ()),
            set(impl._abc_negative_cache or ()), impl._abc_negative_cache_version)


def _reset_registry(self, /):
    """Internal ABC helper to reset registry of a given class.

Should be only used by refleak.py"""
    impl = _get_impl(self)
    if impl._abc_registry is not None:
        impl._abc_registry.clear()


def _reset_caches(self, /):
    """Internal ABC helper to reset both caches of a given class.

Should be only used by refleak.py"""
    impl = _get_impl(self)
    if impl._abc_cache is not None:
        impl._abc_cache.clear()
    if impl._abc_negative_cache is not None:
        impl._abc_negative_cache.clear()


def get_cache_token():
    """Returns the current ABC cache token.

The token is an opaque object (supporting equality testing) identifying
the current version of the ABC cache for virtual subclasses.  The token
changes with every call to register() on any ABC."""
    return _invalidation_counter
