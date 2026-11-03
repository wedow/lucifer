"""The constant cache shared by codegen, flowgraph and assemble.

Port of _PyCode_ConstantKey (Objects/codeobject.c) and of const_cache_insert,
merge_consts_recursive and _PyCompile_ConstCacheMergeOne (Python/compile.c).

The cache maps a constant key to itself in C; tuples are mutated in place
there.  Python tuples are immutable, so the cache maps the first key seen to
the canonical key, whose item 1 is the merged constant.
"""
from math import copysign
from types import CodeType


def constant_key(op):
    """_PyCode_ConstantKey: a key equal only for interchangeable constants."""
    t = type(op)
    # None and Ellipsis are singletons.
    if op is None or op is Ellipsis or t is int or t is str or t is CodeType:
        # Objects of these types are always different from object of other
        # type and from tuples.
        return op
    if t is bool or t is bytes:
        # Make booleans different from integers 0 and 1.
        return (t, op)
    if t is float:
        # Make the tuple different in the -0.0 case from all others.
        if op == 0.0 and copysign(1.0, op) < 0.0:
            return (t, op, None)
        return (t, op)
    if t is complex:
        # All four complex zeros must be distinguished.
        real_negzero = op.real == 0.0 and copysign(1.0, op.real) < 0.0
        imag_negzero = op.imag == 0.0 and copysign(1.0, op.imag) < 0.0
        if real_negzero and imag_negzero:
            return (t, op, True)
        if imag_negzero:
            return (t, op, False)
        if real_negzero:
            return (t, op, None)
        return (t, op)
    if t is tuple:
        return (tuple(constant_key(item) for item in op), op)
    if t is frozenset:
        return (frozenset(tuple(constant_key(item) for item in op)), op)
    if t is slice:
        return (slice(constant_key(op.start), constant_key(op.stop), constant_key(op.step)), op)
    # For other types, use the object identifier as a unique identifier
    # to ensure that they are seen as unequal.
    return (id(op), op)


def const_cache_insert(const_cache, o, recursive):
    """Merge const o and return its constant key.

    If recursive, merge all elements too if o is a tuple or frozen set.
    """
    # None and Ellipsis are immortal objects, and key is the singleton.
    if o is None or o is Ellipsis:
        return o
    key = constant_key(o)
    t = const_cache.get(key)
    if t is not None:
        return t
    const_cache[key] = key
    if not recursive:
        return key
    # We registered o in const_cache.  When o is a tuple or frozenset, we
    # want to merge its items too.
    if type(o) is tuple:
        items = []
        for item in o:
            u = const_cache_insert(const_cache, item, recursive)
            items.append(u[1] if type(u) is tuple else u)
        if any(v is not item for v, item in zip(items, o)):
            const_cache[key] = key = (key[0], tuple(items))
    elif type(o) is frozenset:
        if not o:  # empty frozenset should not be re-created.
            return key
        items = []
        for item in o:
            k = const_cache_insert(const_cache, item, recursive)
            items.append(k[1] if type(k) is tuple else k)
        # Instead of rewriting o, we create new frozenset and embed in the
        # key tuple.  Callers get the merged frozenset from the key tuple.
        const_cache[key] = key = (key[0], frozenset(tuple(items)))
    return key


def merge_consts_recursive(const_cache, o):
    return const_cache_insert(const_cache, o, True)


def const_cache_merge_one(const_cache, obj):
    """_PyCompile_ConstCacheMergeOne: obj merged with the cache, without recursion."""
    key = const_cache_insert(const_cache, obj, False)
    return key[1] if type(key) is tuple else key
