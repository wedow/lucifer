"""_abc (Modules/_abc.c): the caches and registry behind abc.ABCMeta.

Each ABC's positive and negative caches and registry are dicts from a
class's id to a weak reference to it: a check is one int-keyed lookup and
a dereference, where _py_abc makes a weak reference and hashes it through
a WeakSet. An entry leaves when its class dies, and a stale id never
matches because the reference must still name the class asked about."""

import _weakref

_invalidation_counter = 0


class _abc_data:
    """Internal state held by ABC machinery."""
    __slots__ = ('_abc_registry', '_abc_cache', '_abc_negative_cache', '_abc_negative_cache_version')

    def __init__(self):
        self._abc_registry = _WeakIdSet()
        self._abc_cache = _WeakIdSet()
        self._abc_negative_cache = _WeakIdSet()
        self._abc_negative_cache_version = _invalidation_counter


class _WeakIdSet:
    """A set of classes, held weakly, keyed by identity."""
    __slots__ = ('refs', '_selfref', '__weakref__')

    def __init__(self):
        self.refs = {}
        self._selfref = _weakref.ref(self)

    def __contains__(self, cls):
        ref = self.refs.get(id(cls))
        return ref is not None and ref() is cls

    def add(self, cls):
        key, selfref = id(cls), self._selfref

        def gone(ref):
            owner = selfref()
            if owner is not None and owner.refs.get(key) is ref:
                del owner.refs[key]
        self.refs[key] = _weakref.ref(cls, gone)

    def clear(self):
        self.refs.clear()

    def __iter__(self):
        for ref in list(self.refs.values()):
            cls = ref()
            if cls is not None:
                yield cls

    def as_refs(self):
        return {ref for ref in self.refs.values() if ref() is not None}


def get_cache_token():
    """Returns the current ABC cache token.

    The token is an opaque object (supporting equality testing) identifying the
    current version of the ABC cache for virtual subclasses. The token changes
    with every call to register() on any ABC."""
    return _invalidation_counter


def _get_impl(cls):
    impl = cls._abc_impl
    if type(impl) is not _abc_data:
        raise TypeError('_abc_impl is set to a wrong type')
    return impl


def _abc_init(cls):
    """Internal ABC helper for class set-up. Should be never used outside abc module."""
    abstracts = {name for name, value in cls.__dict__.items() if getattr(value, '__isabstractmethod__', False)}
    for base in cls.__bases__:
        for name in getattr(base, '__abstractmethods__', ()):
            value = getattr(cls, name, None)
            if getattr(value, '__isabstractmethod__', False):
                abstracts.add(name)
    cls.__abstractmethods__ = frozenset(abstracts)
    cls._abc_impl = _abc_data()
    if isinstance(cls, type) and '__abc_tpflags__' in cls.__dict__:
        flags = cls.__dict__['__abc_tpflags__']
        if not isinstance(flags, int):
            raise TypeError('__abc_tpflags__ must be an int')
        type.__delattr__(cls, '__abc_tpflags__')


def _abc_register(cls, subclass):
    """Internal ABC helper for subclasss registration. Should be never used outside abc module."""
    global _invalidation_counter
    if not isinstance(subclass, type):
        raise TypeError('Can only register classes')
    if issubclass(subclass, cls):
        return subclass         # Already a subclass
    # Subtle: test for cycles *after* testing for "already a subclass";
    # this means we allow X.register(X) and interpret it as a no-op.
    if issubclass(cls, subclass):
        # This would create a cycle, which is bad for the algorithm below
        raise RuntimeError('Refusing to create an inheritance cycle')
    _get_impl(cls)._abc_registry.add(subclass)
    _invalidation_counter += 1      # Invalidate negative cache
    return subclass


def _abc_instancecheck(cls, instance):
    """Internal ABC helper for instance checks. Should be never used outside abc module."""
    impl = _get_impl(cls)
    subclass = instance.__class__
    if subclass in impl._abc_cache:
        return True
    subtype = type(instance)
    if subtype is subclass:
        if (impl._abc_negative_cache_version == _invalidation_counter
                and subclass in impl._abc_negative_cache):
            return False
        # Fall back to the subclass check.
        return cls.__subclasscheck__(subclass)
    return any(cls.__subclasscheck__(c) for c in (subclass, subtype))


def _abc_subclasscheck(cls, subclass):
    """Internal ABC helper for subclasss checks. Should be never used outside abc module."""
    if not isinstance(subclass, type):
        raise TypeError('issubclass() arg 1 must be a class')
    impl = _get_impl(cls)
    # Check cache
    if subclass in impl._abc_cache:
        return True
    # Check negative cache; may have to invalidate
    if impl._abc_negative_cache_version < _invalidation_counter:
        # Invalidate the negative cache
        impl._abc_negative_cache = _WeakIdSet()
        impl._abc_negative_cache_version = _invalidation_counter
    elif subclass in impl._abc_negative_cache:
        return False
    # Check the subclass hook
    ok = cls.__subclasshook__(subclass)
    if ok is not NotImplemented:
        if not isinstance(ok, bool):
            raise AssertionError('__subclasshook__ must return either False, True, or NotImplemented')
        if ok:
            impl._abc_cache.add(subclass)
        else:
            impl._abc_negative_cache.add(subclass)
        return ok
    # Check if it's a direct subclass
    if cls in getattr(subclass, '__mro__', ()):
        impl._abc_cache.add(subclass)
        return True
    # Check if it's a subclass of a registered class (recursive)
    for rcls in impl._abc_registry:
        if issubclass(subclass, rcls):
            impl._abc_cache.add(subclass)
            return True
    # Check if it's a subclass of a subclass (recursive)
    for scls in cls.__subclasses__():
        if issubclass(subclass, scls):
            impl._abc_cache.add(subclass)
            return True
    # No dice; update negative cache
    impl._abc_negative_cache.add(subclass)
    return False


def _get_dump(cls):
    """Internal ABC helper for cache and registry debugging."""
    impl = _get_impl(cls)
    return (impl._abc_registry.as_refs(), impl._abc_cache.as_refs(), impl._abc_negative_cache.as_refs(),
            impl._abc_negative_cache_version)


def _reset_registry(cls):
    """Internal ABC helper to reset registry of a given class."""
    _get_impl(cls)._abc_registry.clear()


def _reset_caches(cls):
    """Internal ABC helper to reset both caches of a given class."""
    impl = _get_impl(cls)
    impl._abc_cache.clear()
    impl._abc_negative_cache.clear()
