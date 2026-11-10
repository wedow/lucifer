"""set and frozenset (Objects/setobject.c) over the table of
runtime/object/set.h, and the set iterator. The primitives of
runtime/prim/set.c probe and commit; this file runs what needs Python:
hashing, __eq__ on the candidates a probe hands back (re-validated after
the comparison, as set_lookkey and set_add_entry do), the algebra with its
choices of which operand to walk (they decide the order a result iterates
in), and the error texts."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type, list_append,
    repr_enter, repr_leave, is_subtype, kernel_set, kernel_twin,
    set_new, set_probe, set_add_entry, set_discard_entry, set_resize,
    set_clear, set_pop, set_swap, set_merge_clean, set_info, set_hash_impl, set_reset_hash, set_add_simple,
    set_sizeof, setiter_new, setiter_next, setiter_len, setiter_copy,
)
from _lucifer import _boot, _format, operators, iter as _iter, list as _list, dict as _dict

kernel_type = _boot.kernel_type
hash_of = operators.hash_of
decimal = operators.decimal
to_repr = _format.to_repr
to_str = _format.to_str
key_error = _dict.key_error
next_set_entry = _dict.next_set_entry
next_dict_entry = _dict.next_entry

__all__ = ()

# set_info fields
FILL, USED, MASK, FINGER, HASH = 0, 1, 2, 3, 4


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def is_set(x):
    """PyAnySet_Check."""
    return kind(x, 'set')


def is_exact_set(x):
    """PyAnySet_CheckExact."""
    return type(x) is set or type(x) is frozenset


def set_key_hash(key):
    """PyObject_Hash of a set element, with set_unhashable_type's wording."""
    try:
        return hash_of(key)
    except TypeError as exc:
        failure = exc
    raise_unhashable(key, failure)


def raise_unhashable(key, failure):
    if type(failure) is not TypeError:
        raise failure
    error(TypeError, _cat("cannot use '", _name(key), "' as a set element (", to_str(failure), ')'))


def probe_hash(key):
    """The hash of key for a lookup: an unhashable set stands for the frozenset
    of its elements (set_contains_lock_held)."""
    try:
        return hash_of(key)
    except TypeError as exc:
        failure = exc
    if is_subtype(type(key), set):
        return set_hash_impl(key, False)
    raise_unhashable(key, failure)


def contains_entry(s, key, h):
    """Whether s holds a key equal to key. A probe that meets a key it cannot
    judge natively returns it, and __eq__ runs here; judged holds the ones
    found unequal, which the next probe passes over."""
    judged = ()
    while True:
        r = set_probe(s, key, h, judged)
        if type(r) is bool:
            return r
        cand = r[0]
        if cand is key or cand == key:
            return True
        judged += (cand,)


def add_entry(s, key, h):
    """set_add_entry: the probe and the commit are one critical section, so
    a candidate is judged outside it and the commit starts over, where an
    equal key is accepted as found."""
    judged = ()
    while True:
        r = set_add_entry(s, key, h, judged)
        if type(r) is bool:
            return
        cand = r[0]
        if cand is key or cand == key:
            return
        judged += (cand,)


def discard_entry(s, key, h):
    """Whether key was in s (and is no longer). A key __eq__ judged equal is
    removed by identity; if another thread removed it first, the search starts
    over."""
    judged = ()
    while True:
        r = set_discard_entry(s, key, h, judged, False)
        if type(r) is bool:
            return r
        cand = r[0]
        if cand is key or cand == key:
            if set_discard_entry(s, cand, h, (), True):
                return True
            judged = ()
        else:
            judged += (cand,)


def add_key(s, key):
    add_entry(s, key, set_key_hash(key))


def contains_key(s, key):
    return contains_entry(s, key, set_key_hash(key))


def discard_key(s, key):
    return discard_entry(s, key, set_key_hash(key))


# Construction and update

def make_set(cls, iterable):
    s = set_new(cls)
    if iterable is not MISSING:
        update_internal(s, iterable)
    return s


def make_basetype(cls, iterable):
    """make_new_set_basetype: a set or frozenset, whatever subclass cls is."""
    if cls is not set and cls is not frozenset:
        cls = set if is_subtype(cls, set) else frozenset
    return make_set(cls, iterable)


def merge(so, other):
    """set_merge_lock_held: the keys of the set other into so."""
    if other is so or size(other) == 0:
        return
    if (set_info(so, FILL) + size(other)) * 5 >= set_info(so, MASK) * 3:
        set_resize(so, (size(so) + size(other)) * 2)
    if set_info(so, FILL) == 0 and set_merge_clean(so, other):
        return
    e = next_set_entry(other, 0)
    while e is not None:
        add_entry(so, e[1], e[2])
        e = next_set_entry(other, e[0])


def update_dict(so, other):
    n = size(other)
    if (set_info(so, FILL) + n) * 5 >= set_info(so, MASK) * 3:
        set_resize(so, (size(so) + n) * 2)
    e = next_dict_entry(other, 0)
    while e is not None:
        add_entry(so, e[1], e[3])
        e = next_dict_entry(other, e[0])


get_iter = _iter.get_iter
next_item = _iter.next_item


def update_iterable(so, other):
    it = get_iter(other)
    while True:
        key = next_item(it)
        if key is MISSING:
            return
        if not set_add_simple(so, key):
            return _update_rest(so, it, key)


def _update_rest(so, it, key):
    """update_iterable from a key the runtime cannot add itself on: the
    kernel's adds, out of the loop that compiles with the runtime's."""
    while key is not MISSING:
        add_key(so, key)
        key = next_item(it)


def update_lock_held(so, other):
    if is_set(other):
        merge(so, other)
    elif type(other) is dict:
        update_dict(so, other)
    else:
        update_iterable(so, other)


def _set_init1(self, iterable):
    """set.__init__ with one argument, also its twin (kernel_twin): another
    set or a dict merged, anything else iterated here, a frame less deep, so
    that a generator resumes in line."""
    if set_info(self, FILL):
        clear(self)
    set_reset_hash(self)
    if kind(iterable, 'set') or type(iterable) is dict:
        update_internal(self, iterable)
    else:
        update_iterable(self, iterable)


def update_internal(so, other):
    if is_set(other) and other is so:
        return
    update_lock_held(so, other)


def copy_set(so):
    copy = make_basetype(type(so), MISSING)
    merge(copy, so)
    return copy


def set_or(a, b):
    if not is_set(a) or not is_set(b):
        return NotImplemented
    result = copy_set(a)
    if a is not b:
        update_internal(result, b)
    return result


def union(so, others):
    result = make_basetype(type(so), so)
    for other in others:
        if other is not so:
            update_internal(result, other)
    return result


# Intersection

def intersection(so, other):
    if so is other:
        return copy_set(so)
    result = make_basetype(type(so), MISSING)
    if is_set(other):
        if size(other) > size(so):
            so, other = other, so
        e = next_set_entry(other, 0)
        while e is not None:
            if contains_entry(so, e[1], e[2]):
                add_entry(result, e[1], e[2])
            e = next_set_entry(other, e[0])
        return result
    it = _iter.get_iter(other)
    while True:
        key = _iter.next_item(it)
        if key is MISSING:
            return result
        h = hash_of(key)
        if contains_entry(so, key, h):
            add_entry(result, key, h)
            if size(result) >= size(so):
                return result


def intersection_multi(so, others):
    if size(others) == 0:
        return copy_set(so)
    result = so
    for other in others:
        result = intersection(result, other)
    return result


# Difference

def clear(so):
    set_clear(so)


def difference_update_internal(so, other):
    if so is other:
        clear(so)
        return
    if is_set(other):
        if (size(other) >> 3) > size(so):
            other = intersection(so, other)
        e = next_set_entry(other, 0)
        while e is not None:
            discard_entry(so, e[1], e[2])
            e = next_set_entry(other, e[0])
    else:
        it = _iter.get_iter(other)
        while True:
            key = _iter.next_item(it)
            if key is MISSING:
                break
            discard_key(so, key)
    if set_info(so, FILL) - size(so) <= set_info(so, MASK) // 4:
        return
    set_resize(so, size(so) * 2 if size(so) > 50000 else size(so) * 4)


def copy_and_difference(so, other):
    result = copy_set(so)
    difference_update_internal(result, other)
    return result


def difference(so, other):
    if is_set(other) or type(other) is dict:
        other_size = size(other)
    else:
        return copy_and_difference(so, other)
    if (size(so) >> 2) > other_size:
        return copy_and_difference(so, other)
    result = make_basetype(type(so), MISSING)
    e = next_set_entry(so, 0)
    while e is not None:
        if type(other) is dict:
            present = _dict.lookup(other, e[1], e[2]) >= 0
        else:
            present = contains_entry(other, e[1], e[2])
        if not present:
            add_entry(result, e[1], e[2])
        e = next_set_entry(so, e[0])
    return result


def difference_multi(so, others):
    if size(others) == 0:
        return copy_set(so)
    result = difference(so, others[0])
    i = 1
    while i < size(others):
        difference_update_internal(result, others[i])
        i += 1
    return result


# Symmetric difference

def symmetric_difference_update_dict(so, other):
    e = next_dict_entry(other, 0)
    while e is not None:
        if not discard_entry(so, e[1], e[3]):
            add_entry(so, e[1], e[3])
        e = next_dict_entry(other, e[0])


def symmetric_difference_update_set(so, other):
    e = next_set_entry(other, 0)
    while e is not None:
        if not discard_entry(so, e[1], e[2]):
            add_entry(so, e[1], e[2])
        e = next_set_entry(other, e[0])


def symmetric_difference_update(so, other):
    if so is other:
        clear(so)
    elif type(other) is dict:
        symmetric_difference_update_dict(so, other)
    elif is_set(other):
        symmetric_difference_update_set(so, other)
    else:
        symmetric_difference_update_set(so, make_basetype(type(so), other))


def symmetric_difference(so, other):
    result = make_basetype(type(so), MISSING)
    update_lock_held(result, other)
    symmetric_difference_update_set(result, so)
    return result


# Comparison

def issubset(so, other):
    if not is_set(other):
        return size(intersection(so, other)) == size(so)
    if size(so) > size(other):
        return False
    e = next_set_entry(so, 0)
    while e is not None:
        if not contains_entry(other, e[1], e[2]):
            return False
        e = next_set_entry(so, e[0])
    return True


def issuperset(so, other):
    if is_set(other):
        return issubset(other, so)
    it = _iter.get_iter(other)
    while True:
        key = _iter.next_item(it)
        if key is MISSING:
            return True
        if not contains_key(so, key):
            return False


def isdisjoint(so, other):
    if so is other:
        return size(so) == 0
    if is_exact_set(other):
        if size(other) > size(so):
            so, other = other, so
        e = next_set_entry(other, 0)
        while e is not None:
            if contains_entry(so, e[1], e[2]):
                return False
            e = next_set_entry(other, e[0])
        return True
    it = _iter.get_iter(other)
    while True:
        key = _iter.next_item(it)
        if key is MISSING:
            return True
        if contains_key(so, key):
            return False


def richcompare(v, w, op):
    if not is_set(w):
        return NotImplemented
    if op == 2:
        if size(v) != size(w):
            return False
        vh = set_info(v, HASH)
        wh = set_info(w, HASH)
        if vh != -1 and wh != -1 and vh != wh:
            return False
        return issubset(v, w)
    if op == 3:
        return not richcompare(v, w, 2)
    if op == 1:
        return issubset(v, w)
    if op == 5:
        return issuperset(v, w)
    if op == 0:
        return size(v) < size(w) and issubset(v, w)
    return size(v) > size(w) and issuperset(v, w)


def contains(so, key):
    return contains_entry(so, key, probe_hash(key))


def remove(so, key):
    if not discard_entry(so, key, probe_hash(key)):
        key_error(key)


def discard(so, key):
    discard_entry(so, key, probe_hash(key))


def frozenset_hash(self):
    h = set_info(self, HASH)
    return h if h != -1 else set_hash_impl(self, True)


def set_repr(self):
    name = type_tp_name(type(self))
    if repr_enter(self):
        return _cat(name, '(...)')
    try:
        if size(self) == 0:
            return _cat(name, '()')
        keys = []
        e = next_set_entry(self, 0)
        while e is not None:
            list_append(keys, e[1])
            e = next_set_entry(self, e[0])
        inner = to_repr(keys)[1:-1]
    finally:
        repr_leave(self)
    return _cat('{', inner, '}') if type(self) is set else _cat(name, '({', inner, '})')


def set_reduce(self):
    try:
        state = self.__dict__
    except AttributeError:
        state = None
    return (type(self), (_list.list_of(self),), state if state else None)


def unpack_one(cls, args):
    """PyArg_UnpackTuple(args, tp_name, 0, 1): the iterable, or MISSING."""
    n = size(args)
    if n > 1:
        error(TypeError, _cat(type_tp_name(cls), ' expected at most 1 argument, got ', decimal(n)))
    return args[0] if n == 1 else MISSING


@kernel_type(static_type('set'))
class set:
    """Build an unordered collection of unique elements."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not set:
            _boot.check_new(cls, set)
        return set_new(cls)

    def __init__(self, /, *args, **kwargs):
        if kwargs and (type(self) is set or type_lookup(type(self), '__new__') is type_lookup(set, '__new__')):
            error(TypeError, 'set() takes no keyword arguments')
        iterable = unpack_one(type(self), args)
        if iterable is not MISSING:
            _set_init1(self, iterable)
        else:
            if set_info(self, FILL):
                clear(self)
            set_reset_hash(self)

    kernel_twin(__init__, _set_init1)

    def __repr__(self):
        return set_repr(self)

    __hash__ = None

    def __len__(self):
        return size(self)

    def __iter__(self):
        return setiter_new(self)

    def __contains__(self, key, /):
        """x.__contains__(y) <==> y in x."""
        return contains(self, key)

    def __lt__(self, other):
        return richcompare(self, other, 0)

    def __le__(self, other):
        return richcompare(self, other, 1)

    def __eq__(self, other):
        return richcompare(self, other, 2)

    def __ne__(self, other):
        return richcompare(self, other, 3)

    def __gt__(self, other):
        return richcompare(self, other, 4)

    def __ge__(self, other):
        return richcompare(self, other, 5)

    def __or__(self, other):
        return set_or(self, other)

    def __ror__(self, other):
        return set_or(other, self)

    def __ior__(self, other):
        if not is_set(other):
            return NotImplemented
        update_internal(self, other)
        return self

    def __sub__(self, other):
        return difference(self, other) if is_set(other) else NotImplemented

    def __rsub__(self, other):
        return difference(other, self) if is_set(other) else NotImplemented

    def __isub__(self, other):
        if not is_set(other):
            return NotImplemented
        difference_update_internal(self, other)
        return self

    def __and__(self, other):
        return intersection(self, other) if is_set(other) else NotImplemented

    def __rand__(self, other):
        return intersection(other, self) if is_set(other) else NotImplemented

    def __iand__(self, other):
        if not is_set(other):
            return NotImplemented
        set_swap(self, intersection(self, other))
        return self

    def __xor__(self, other):
        return symmetric_difference(self, other) if is_set(other) else NotImplemented

    def __rxor__(self, other):
        return symmetric_difference(other, self) if is_set(other) else NotImplemented

    def __ixor__(self, other):
        if not is_set(other):
            return NotImplemented
        symmetric_difference_update(self, other)
        return self

    def add(self, object, /):
        """Add an element to a set.

This has no effect if the element is already present."""
        add_key(self, object)

    def clear(self):
        """Remove all elements from this set."""
        clear(self)

    def copy(self):
        """Return a shallow copy of a set."""
        return copy_set(self)

    def discard(self, object, /):
        """Remove an element from a set if it is a member.

Unlike set.remove(), the discard() method does not raise
an exception when an element is missing from the set."""
        discard(self, object)

    def remove(self, object, /):
        """Remove an element from a set; it must be a member.

If the element is not a member, raise a KeyError."""
        remove(self, object)

    def pop(self):
        """Remove and return an arbitrary set element.

Raises KeyError if the set is empty."""
        key = set_pop(self)
        if key is MISSING:
            error(KeyError, 'pop from an empty set')
        return key

    def update(self, *others):
        """Update the set, adding elements from all others."""
        for other in others:
            update_internal(self, other)

    def union(self, *others):
        """Return a new set with elements from the set and all others."""
        return union(self, others)

    def difference(self, *others):
        """Return a new set with elements in the set that are not in the others."""
        return difference_multi(self, others)

    def difference_update(self, *others):
        """Update the set, removing elements found in others."""
        for other in others:
            difference_update_internal(self, other)

    def intersection(self, *others):
        """Return a new set with elements common to the set and all others."""
        return intersection_multi(self, others)

    def intersection_update(self, *others):
        """Update the set, keeping only elements found in it and all others."""
        set_swap(self, intersection_multi(self, others))

    def symmetric_difference(self, other, /):
        """Return a new set with elements in either the set or other but not both."""
        return symmetric_difference(self, other)

    def symmetric_difference_update(self, other, /):
        """Update the set, keeping only elements found in either set, but not in both."""
        symmetric_difference_update(self, other)

    def isdisjoint(self, other, /):
        """Return True if two sets have a null intersection."""
        return isdisjoint(self, other)

    def issubset(self, other, /):
        """Report whether another set contains this set."""
        return issubset(self, other)

    def issuperset(self, other, /):
        """Report whether this set contains another set."""
        return issuperset(self, other)

    def __reduce__(self):
        """Return state information for pickling."""
        return set_reduce(self)

    def __sizeof__(self):
        """S.__sizeof__() -> size of S in memory, in bytes."""
        return set_sizeof(self)


@kernel_type(static_type('frozenset'))
class frozenset:
    """Build an immutable unordered collection of unique elements."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not frozenset:
            _boot.check_new(cls, frozenset)
        if kwargs and (cls is frozenset or type_lookup(cls, '__init__') is type_lookup(frozenset, '__init__')):
            error(TypeError, 'frozenset() takes no keyword arguments')
        iterable = unpack_one(cls, args)
        if cls is frozenset and iterable is not MISSING and type(iterable) is frozenset:
            return iterable
        return make_set(cls, iterable)

    def __repr__(self):
        return set_repr(self)

    def __hash__(self):
        return frozenset_hash(self)

    def __len__(self):
        return size(self)

    def __iter__(self):
        return setiter_new(self)

    def __contains__(self, key, /):
        """x.__contains__(y) <==> y in x."""
        return contains(self, key)

    def __lt__(self, other):
        return richcompare(self, other, 0)

    def __le__(self, other):
        return richcompare(self, other, 1)

    def __eq__(self, other):
        return richcompare(self, other, 2)

    def __ne__(self, other):
        return richcompare(self, other, 3)

    def __gt__(self, other):
        return richcompare(self, other, 4)

    def __ge__(self, other):
        return richcompare(self, other, 5)

    def __or__(self, other):
        return set_or(self, other)

    def __ror__(self, other):
        return set_or(other, self)

    def __sub__(self, other):
        return difference(self, other) if is_set(other) else NotImplemented

    def __rsub__(self, other):
        return difference(other, self) if is_set(other) else NotImplemented

    def __and__(self, other):
        return intersection(self, other) if is_set(other) else NotImplemented

    def __rand__(self, other):
        return intersection(other, self) if is_set(other) else NotImplemented

    def __xor__(self, other):
        return symmetric_difference(self, other) if is_set(other) else NotImplemented

    def __rxor__(self, other):
        return symmetric_difference(other, self) if is_set(other) else NotImplemented

    def copy(self):
        """Return a shallow copy of a set."""
        return self if type(self) is frozenset else copy_set(self)

    def union(self, *others):
        """Return a new set with elements from the set and all others."""
        return union(self, others)

    def difference(self, *others):
        """Return a new set with elements in the set that are not in the others."""
        return difference_multi(self, others)

    def intersection(self, *others):
        """Return a new set with elements common to the set and all others."""
        return intersection_multi(self, others)

    def symmetric_difference(self, other, /):
        """Return a new set with elements in either the set or other but not both."""
        return symmetric_difference(self, other)

    def isdisjoint(self, other, /):
        """Return True if two sets have a null intersection."""
        return isdisjoint(self, other)

    def issubset(self, other, /):
        """Report whether another set contains this set."""
        return issubset(self, other)

    def issuperset(self, other, /):
        """Report whether this set contains another set."""
        return issuperset(self, other)

    def __reduce__(self):
        """Return state information for pickling."""
        return set_reduce(self)

    def __sizeof__(self):
        """S.__sizeof__() -> size of S in memory, in bytes."""
        return set_sizeof(self)


@kernel_type(static_type('set_iterator'))
class set_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        key = setiter_next(self)
        if key is MISSING:
            raise StopIteration
        return key

    def __length_hint__(self):
        """Private method returning an estimate of len(list(it))."""
        return setiter_len(self)

    def __reduce__(self):
        """Return state information for pickling."""
        return (iter, (_list.list_of(setiter_copy(self)),))


def build_set(items):
    """BUILD_SET: a set of the items of a tuple."""
    s = set_new(set)
    for item in items:
        add_key(s, item)
    return s


def set_add(s, item):
    """SET_ADD."""
    add_key(s, item)


kernel_set('build_set', build_set)
kernel_set('set_add', set_add)
kernel_set('set_update', update_internal)


_iter.next_missing_set(set_iterator, setiter_next)
