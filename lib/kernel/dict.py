"""dict (Objects/dictobject.c) over the keys table of runtime/object/dict.h,
its iterators and its views. The primitives of runtime/prim/dict.c probe and
commit; this file is everything that runs Python: hashing, __eq__ on the
candidates a probe hands back (outside any commit region, with CPython's
re-validation when the comparison mutated the dict), the hooks, the
protocols and the error texts."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type, list_append,
    repr_enter, repr_leave, set_slot, kernel_set,
    dict_new, dict_probe, dict_find, dict_put, dict_key, dict_value, dict_entry, dict_stamp, dict_insert, dict_replace, dict_remove,
    dict_popitem, dict_clear, dict_next, dict_prev, dict_keys_id, dict_copy, dict_merge,
    dict_sizeof, dictiter_new, dictiter_next, dictiter_len, dictiter_copy, dictview_new,
    dictview_dict, set_next, set_entry, set_entries,
)
from _lucifer import _boot, _format, operators, iter as _iter, list as _list, str as _str

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
call_special = _boot.call_special
hash_of = operators.hash_of
decimal = operators.decimal
to_repr = _format.to_repr
to_str = _format.to_str
_args = _str._args

__all__ = ()

_SSIZE_MAX = 9223372036854775807


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def key_error(key):
    """_PyErr_SetKeyError: the key is the one argument, even when it is a tuple."""
    error(KeyError, (key,))


def key_hash(key):
    """PyObject_Hash of a dict key, with dict_unhashable_type's wording."""
    try:
        return hash_of(key)
    except TypeError as exc:
        failure = exc
    if type(failure) is not TypeError:
        raise failure
    error(TypeError, _cat("cannot use '", _name(key), "' as a dict key (", to_str(failure), ')'))


def has_keys(obj):
    try:
        obj.keys
    except AttributeError:
        return False
    return True


def lookup(d, key, h):
    """The entry number of key in d, or -1. A probe that meets a key it cannot
    judge natively returns it with its position in the probe sequence; __eq__
    runs here, and when it changed the keys table or the entry the probe
    starts over, as _Py_dict_lookup does. A probe that resumes past an unequal
    candidate counts only if the table and the candidate stayed: in another
    table, or with the candidate deleted, the skip to it passes the key."""
    steps = 0
    while True:
        if steps == 0:
            table = dict_keys_id(d)
        r = dict_probe(d, key, h, steps)
        if type(r) is int:
            if steps == 0 or dict_keys_id(d) == table and dict_key(d, cand) is startkey:
                return r
            steps = 0
            continue
        cand = r[0]
        startkey = dict_key(d, cand)
        if startkey is MISSING:
            steps = 0
            continue
        equal = True if startkey is key or startkey == key else False
        if dict_keys_id(d) == table and dict_key(d, cand) is startkey:
            if equal:
                return cand
            steps = r[1]
        else:
            steps = 0


def find_value(d, key, h):
    """The value of key, or MISSING. The entry number is the table's that the
    lookup probed, so the value counts only if that table is still d's."""
    while True:
        table = dict_keys_id(d)
        ix = lookup(d, key, h)
        if ix < 0:
            return MISSING
        value = dict_value(d, ix)
        if value is not MISSING and dict_keys_id(d) == table:
            return value


def store(d, key, h, value):
    """insertdict: d[key] = value for a key of hash h. The commit takes the
    stamp read before the lookup, and fails when another thread resized the
    dict or added a key since: the entry number or the absence is stale."""
    while True:
        stamp = dict_stamp(d)
        ix = lookup(d, key, h)
        if ix < 0:
            if dict_insert(d, stamp, key, h, value):
                return
        elif dict_replace(d, stamp, ix, value):
            return


def delete(d, key, h):
    """Removes key and returns its value, or MISSING when it is absent."""
    while True:
        stamp = dict_stamp(d)
        ix = lookup(d, key, h)
        if ix < 0:
            return MISSING
        value = dict_remove(d, stamp, ix)
        if value is not MISSING:
            return value


def contains(d, key):
    found = dict_find(d, key)
    if found is NotImplemented:
        return lookup(d, key, key_hash(key)) >= 0
    return found is not MISSING


def setitem(d, key, value):
    if dict_put(d, key, value) is NotImplemented:
        store(d, key, key_hash(key), value)


def delitem(d, key):
    if delete(d, key, key_hash(key)) is MISSING:
        key_error(key)


def setdefault(d, key, default):
    h = key_hash(key)
    while True:
        stamp = dict_stamp(d)
        ix = lookup(d, key, h)
        if ix < 0:
            if dict_insert(d, stamp, key, h, default):
                return default
            continue
        value = dict_value(d, ix)
        if value is not MISSING and dict_keys_id(d) == stamp >> 32:
            return value


def next_entry(d, i):
    """(the position after the entry, key, value, hash) of the live entry of d
    at or after position i, or None. Loops over a dict that changes under them
    call it again with the position it returned."""
    while True:
        i = dict_next(d, i)
        if i < 0:
            return None
        e = dict_entry(d, i)
        if e is not MISSING:
            return e
        i += 1


def next_set_entry(s, i):
    """The same for the entries of a set: (position, key, hash), or None."""
    while True:
        i = set_next(s, i)
        if i < 0:
            return None
        e = set_entry(s, i)
        if e is not MISSING:
            return e
        i += 1


# update, merge and fromkeys (dict_merge, merge_from_seq2, _PyDict_FromKeys)

def merge_dict(a, b, override):
    """dict_dict_merge: the entries of the dict b into a; override 0 keeps the
    keys a has, 1 replaces them, 2 raises KeyError for them. The merge runs
    under both dicts' locks; an entry whose key only __eq__ can judge (or, for
    override 2, one a holds) comes back here, is handled outside the locks,
    and the merge goes on after it."""
    stop = dict_merge(a, b, override, 0)
    while stop is not None:
        i, key, value, h, n = stop
        if override == 1:
            store(a, key, h, value)
        elif lookup(a, key, h) >= 0:
            if override != 0:
                key_error(key)
        else:
            store(a, key, h, value)
        if size(b) != n:
            error(RuntimeError, 'dict mutated during update')
        stop = dict_merge(a, b, override, i + 1)


def mapping_keys(o):
    """PyMapping_Keys: o.keys() as a list."""
    if type(o) is dict:
        keys = []
        e = next_entry(o, 0)
        while e is not None:
            list_append(keys, e[1])
            e = next_entry(o, e[0])
        return keys
    out = o.keys()
    if type(out) is list:
        return out
    it = None
    try:
        it = _iter.get_iter(out)
    except TypeError:
        pass
    if it is None:
        error(TypeError, _cat(_name(o), '.keys() returned a non-iterable (type ', _name(out), ')'))
    return _list.list_of(it)


def merge_mapping(a, b, override):
    """The generic dict_merge: b.keys() and b[key]."""
    for key in mapping_keys(b):
        if override != 1:
            if contains(a, key):
                if override == 0:
                    continue
                key_error(key)
        setitem(a, key, operators.getitem(b, key))


def merge(a, b, override):
    if kind(b, 'dict') and type_lookup(type(b), '__iter__') is _DICT_ITER:
        merge_dict(a, b, override)
    else:
        merge_mapping(a, b, override)


def merge_from_seq2(d, seq2, override):
    it = _iter.get_iter(seq2)
    i = 0
    while True:
        item = _iter.next_item(it)
        if item is MISSING:
            return
        try:
            fast = _list.sequence_fast(item, 'object is not iterable')
        except TypeError as exc:
            exc.add_note(_cat('Cannot convert dictionary update sequence element #', decimal(i),
                              ' to a sequence'))
            raise
        n = size(fast)
        if n != 2:
            error(ValueError, _cat('dictionary update sequence element #', decimal(i),
                                   ' has length ', decimal(n), '; 2 is required'))
        key = fast[0]
        value = fast[1]
        if override:
            setitem(d, key, value)
        else:
            setdefault(d, key, value)
        i += 1


def update_arg(d, arg):
    """dict_update_arg: dict.update(arg)."""
    if type(arg) is dict:
        merge(d, arg, 1)
        return
    if has_keys(arg):
        merge(d, arg, 1)
    else:
        merge_from_seq2(d, arg, 1)


def update_common(d, args, kwargs, name):
    n = size(args)
    if n > 1:
        _args(name, args, 0, 1)
    if n == 1:
        update_arg(d, args[0])
    if kwargs:
        merge(d, kwargs, 1)


def build_map(items):
    """BUILD_MAP: the pairs key, value, ... of a tuple, for keys that are not all strs."""
    d = {}
    i = 0
    while i < size(items):
        setitem(d, items[i], items[i + 1])
        i += 2
    return d


def merge_kwargs(d, update, fname):
    """DICT_MERGE: the keywords of f(**update) into the kwargs dict d, which
    holds strs and refuses a key twice."""
    if not kind(update, 'dict') and not has_keys(update):
        error(TypeError, _cat(fname, ' argument after ** must be a mapping, not ', _name(update)))
    for key in mapping_keys(update):
        if not kind(key, 'str'):
            error(TypeError, 'keywords must be strings')
        value = operators.getitem(update, key)
        if contains(d, key):
            error(TypeError, _cat(fname, " got multiple values for keyword argument '", key, "'"))
        setitem(d, key, value)


def update_op(d, update):
    """DICT_UPDATE: {**update}."""
    if not has_keys(update):
        error(TypeError, _cat("'", _name(update), "' object is not a mapping"))
    merge(d, update, 1)


def fromkeys(cls, iterable, value):
    d = cls()
    if type(d) is dict:
        if type(iterable) is dict:
            iterable = dict_copy(iterable)  # the source whole, as under its lock
            e = next_entry(iterable, 0)
            while e is not None:
                store(d, e[1], e[3], value)
                e = next_entry(iterable, e[0])
            return d
        if type(iterable) is set or type(iterable) is frozenset:
            entries = set_entries(iterable)     # the source whole, as under its lock
            for i in range(0, size(entries), 2):
                store(d, entries[i], entries[i + 1], value)
            return d
    it = _iter.get_iter(iterable)
    if type(d) is dict:
        while True:
            key = _iter.next_item(it)
            if key is MISSING:
                return d
            setitem(d, key, value)
    while True:
        key = _iter.next_item(it)
        if key is MISSING:
            return d
        operators.setitem(d, key, value)


def equal(a, b):
    """dict_equal."""
    if size(a) != size(b):
        return False
    e = next_entry(a, 0)
    while e is not None:
        i, key, aval, h = e
        bval = find_value(b, key, h)
        if bval is MISSING:
            return False
        if not (aval is bval or aval == bval):
            return False
        e = next_entry(a, i)
    return True


def _get(self, key, default=None, /):
    """Return the value for key if key is in the dictionary, else default."""
    value = dict_find(self, key)
    if value is NotImplemented:
        value = find_value(self, key, key_hash(key))
    return default if value is MISSING else value


def _items(self):
    """Return a set-like object providing a view on the dict's items."""
    return dictview_new(static_type('dict_items'), self)


# Registered for the call instruction, which compares them: named as the methods.
_get.__qualname__ = 'dict.get'
_items.__qualname__ = 'dict.items'


@kernel_type(static_type('dict'))
class dict:
    """dict() -> new empty dictionary
dict(mapping) -> new dictionary initialized from a mapping object's
    (key, value) pairs
dict(iterable) -> new dictionary initialized as if via:
    d = {}
    for k, v in iterable:
        d[k] = v
dict(**kwargs) -> new dictionary initialized with the name=value pairs
    in the keyword argument list.  For example:  dict(one=1, two=2)"""

    def __new__(cls, /, *args, **kwargs):
        if cls is not dict:
            _boot.check_new(cls, dict)
        return dict_new(cls)

    def __init__(self, /, *args, **kwargs):
        update_common(self, args, kwargs, 'dict')

    def __repr__(self):
        if repr_enter(self):
            return '{...}'
        try:
            parts = []
            e = next_entry(self, 0)
            while e is not None:
                list_append(parts, _cat(to_repr(e[1]), ': ', to_repr(e[2])))
                e = next_entry(self, e[0])
        finally:
            repr_leave(self)
        return _cat('{', str_join(', ', (*parts,)), '}')

    __hash__ = None

    def __eq__(self, other):
        if not kind(other, 'dict'):
            return NotImplemented
        return equal(self, other)

    def __ne__(self, other):
        if not kind(other, 'dict'):
            return NotImplemented
        return not equal(self, other)

    def __lt__(self, other):
        return NotImplemented

    __le__ = __gt__ = __ge__ = __lt__

    def __len__(self):
        return size(self)

    def __getitem__(self, key, /):
        """Return self[key]."""
        value = dict_find(self, key)
        if value is NotImplemented:
            value = find_value(self, key, key_hash(key))
        if value is not MISSING:
            return value
        if type(self) is not dict:
            missing = type_lookup(type(self), '__missing__')
            if missing is not MISSING:
                return call_special(missing, self, key)
        key_error(key)

    def __setitem__(self, key, value):
        setitem(self, key, value)

    def __delitem__(self, key):
        delitem(self, key)

    def __contains__(self, key, /):
        """True if the dictionary has the specified key, else False."""
        return contains(self, key)

    def __iter__(self):
        return dictiter_new(static_type('dict_keyiterator'), self)

    def __reversed__(self):
        """Return a reverse iterator over the dict keys."""
        return dictiter_new(static_type('dict_reversekeyiterator'), self)

    def __sizeof__(self):
        return dict_sizeof(self)

    def __or__(self, other):
        if not kind(other, 'dict'):
            return NotImplemented
        new = dict_copy(self)
        update_arg(new, other)
        return new

    def __ror__(self, other):
        if not kind(other, 'dict'):
            return NotImplemented
        new = dict_copy(other)
        update_arg(new, self)
        return new

    def __ior__(self, other):
        update_arg(self, other)
        return self

    get = _get

    def setdefault(self, key, default=None, /):
        """Insert key with a value of default if key is not in the dictionary.

Return the value for key if key is in the dictionary, else default."""
        return setdefault(self, key, default)

    def pop(self, key, default=MISSING, /):
        """D.pop(k[,d]) -> v, remove specified key and return the corresponding value.

If the key is not found, return the default if given; otherwise,
raise a KeyError."""
        value = MISSING
        if size(self) != 0:
            value = delete(self, key, key_hash(key))
        if value is MISSING:
            if default is MISSING:
                key_error(key)
            return default
        return value

    def popitem(self):
        """Remove and return a (key, value) pair as a 2-tuple.

Pairs are returned in LIFO (last-in, first-out) order.
Raises KeyError if the dict is empty."""
        pair = dict_popitem(self)
        if pair is MISSING:
            error(KeyError, 'popitem(): dictionary is empty')
        return pair

    def keys(self):
        """Return a set-like object providing a view on the dict's keys."""
        return dictview_new(static_type('dict_keys'), self)

    items = _items

    def values(self):
        """Return an object providing a view on the dict's values."""
        return dictview_new(static_type('dict_values'), self)

    def update(self, *args, **kwargs):
        update_common(self, args, kwargs, 'update')

    @classmethod_
    def fromkeys(cls, iterable, value=None, /):
        """Create a new dictionary with keys from iterable and values set to value."""
        return fromkeys(cls, iterable, value)

    def clear(self):
        """Remove all items from the dict."""
        dict_clear(self)

    def copy(self):
        """Return a shallow copy of the dict."""
        return dict_copy(self)


# A dict answers mp_subscript, not sq_item (PySequence_Check is false for it).
set_slot(dict, 'sq_length', 0, None)
set_slot(dict, 'sq_item', 0, None)
set_slot(dict, 'sq_ass_item', 0, None)

_DICT_ITER = type_lookup(dict, '__iter__')


# Iterators

def iterator_iter(self):
    return self


def iterator_next(self):
    v = dictiter_next(self)
    if v is MISSING:
        raise StopIteration
    return v


def iterator_hint(self):
    """Private method returning an estimate of len(list(it))."""
    return dictiter_len(self)


def iterator_reduce(self):
    """Return state information for pickling."""
    return (iter, (_list.list_of(dictiter_copy(self)),))


@kernel_type(static_type('dict_keyiterator'))
class dict_keyiterator:
    __iter__ = iterator_iter
    __next__ = iterator_next
    __length_hint__ = iterator_hint
    __reduce__ = iterator_reduce


@kernel_type(static_type('dict_valueiterator'))
class dict_valueiterator:
    __iter__ = iterator_iter
    __next__ = iterator_next
    __length_hint__ = iterator_hint
    __reduce__ = iterator_reduce


@kernel_type(static_type('dict_itemiterator'))
class dict_itemiterator:
    __iter__ = iterator_iter
    __next__ = iterator_next
    __length_hint__ = iterator_hint
    __reduce__ = iterator_reduce


@kernel_type(static_type('dict_reversekeyiterator'))
class dict_reversekeyiterator:
    __iter__ = iterator_iter
    __next__ = iterator_next
    __length_hint__ = iterator_hint
    __reduce__ = iterator_reduce


@kernel_type(static_type('dict_reversevalueiterator'))
class dict_reversevalueiterator:
    __iter__ = iterator_iter
    __next__ = iterator_next
    __length_hint__ = iterator_hint
    __reduce__ = iterator_reduce


@kernel_type(static_type('dict_reverseitemiterator'))
class dict_reverseitemiterator:
    __iter__ = iterator_iter
    __next__ = iterator_next
    __length_hint__ = iterator_hint
    __reduce__ = iterator_reduce


# Views

def all_contained_in(a, b):
    """Whether every element of the iterable a is in b."""
    it = _iter.get_iter(a)
    while True:
        x = _iter.next_item(it)
        if x is MISSING:
            return True
        if not _iter.contains(b, x, False):
            return False


def is_view_set(x):
    return type(x) is dict_keys or type(x) is dict_items


def view_compare(self, other, op):
    if not kind(other, 'set') and not is_view_set(other):
        return NotImplemented
    len_self = operators.length(self)
    len_other = operators.length(other)
    if op == 2 or op == 3:
        ok = len_self == len_other and all_contained_in(self, other)
        return ok if op == 2 else not ok
    if op == 0:
        return len_self < len_other and all_contained_in(self, other)
    if op == 1:
        return len_self <= len_other and all_contained_in(self, other)
    if op == 4:
        return len_self > len_other and all_contained_in(other, self)
    return len_self >= len_other and all_contained_in(other, self)


def view_repr(self):
    if repr_enter(self):
        return '...'
    try:
        return _cat(type_tp_name(type(self)), '(', to_repr(_list.list_of(self)), ')')
    finally:
        repr_leave(self)


def to_set(x):
    """dictviews_to_set: the set of a view, or of whatever stands in its place."""
    if type(x) is dict_keys:
        d = dictview_dict(x)
        if type(d) is dict:
            x = d
    return set(x)


def view_sub(a, b):
    result = to_set(a)
    result.difference_update(b)
    return result


def view_or(a, b):
    result = to_set(a)
    result.update(b)
    return result


def view_and(a, b):
    """_PyDictView_Intersect; the interpreter swaps the operands when the view is on the right."""
    if not is_view_set(a):
        a, b = b, a
    len_a = operators.length(a)
    if type(b) is set and len_a <= operators.length(b):
        return b.intersection(a)
    if is_view_set(b) and operators.length(b) > len_a:
        a, b = b, a
    result = set()
    it = _iter.get_iter(b)
    while True:
        x = _iter.next_item(it)
        if x is MISSING:
            return result
        if _iter.contains(a, x, False):
            result.add(x)


def items_xor(a, b):
    d1 = dictview_dict(a)
    d2 = dictview_dict(b)
    temp = dict_copy(d1)
    result = set()
    e = next_entry(d2, 0)
    while e is not None:
        i, key, val2, h = e
        val1 = find_value(temp, key, h)
        if val1 is not MISSING and (val1 is val2 or val1 == val2):
            delete(temp, key, h)
        else:
            result.add((key, val2))
        e = next_entry(d2, i)
    result.update(temp.items())
    return result


def view_xor(a, b):
    if type(a) is dict_items and type(b) is dict_items:
        return items_xor(a, b)
    result = to_set(a)
    result.symmetric_difference_update(b)
    return result


def view_isdisjoint(self, other):
    if self is other:
        return operators.length(self) == 0
    if kind(other, 'set') or is_view_set(other):
        if operators.length(other) > operators.length(self):
            self, other = other, self
    it = _iter.get_iter(other)
    while True:
        x = _iter.next_item(it)
        if x is MISSING:
            return True
        if _iter.contains(self, x, False):
            return False


@kernel_type(static_type('dict_keys'))
class dict_keys:
    def __len__(self):
        return size(dictview_dict(self))

    def __repr__(self):
        return view_repr(self)

    def __iter__(self):
        return dictiter_new(static_type('dict_keyiterator'), dictview_dict(self))

    def __reversed__(self):
        """Return a reverse iterator over the dict keys."""
        return dictiter_new(static_type('dict_reversekeyiterator'), dictview_dict(self))

    def __contains__(self, key):
        return contains(dictview_dict(self), key)

    def __lt__(self, other):
        return view_compare(self, other, 0)

    def __le__(self, other):
        return view_compare(self, other, 1)

    def __eq__(self, other):
        return view_compare(self, other, 2)

    def __ne__(self, other):
        return view_compare(self, other, 3)

    def __gt__(self, other):
        return view_compare(self, other, 4)

    def __ge__(self, other):
        return view_compare(self, other, 5)

    __hash__ = None

    def __sub__(self, other):
        return view_sub(self, other)

    def __rsub__(self, other):
        return view_sub(other, self)

    def __and__(self, other):
        return view_and(self, other)

    def __rand__(self, other):
        return view_and(other, self)

    def __xor__(self, other):
        return view_xor(self, other)

    def __rxor__(self, other):
        return view_xor(other, self)

    def __or__(self, other):
        return view_or(self, other)

    def __ror__(self, other):
        return view_or(other, self)

    def isdisjoint(self, other):
        """Return True if the view and the given iterable have a null intersection."""
        return view_isdisjoint(self, other)


@kernel_type(static_type('dict_items'))
class dict_items:
    def __len__(self):
        return size(dictview_dict(self))

    def __repr__(self):
        return view_repr(self)

    def __iter__(self):
        return dictiter_new(static_type('dict_itemiterator'), dictview_dict(self))

    def __reversed__(self):
        """Return a reverse iterator over the dict items."""
        return dictiter_new(static_type('dict_reverseitemiterator'), dictview_dict(self))

    def __contains__(self, item):
        if not kind(item, 'tuple') or size(item) != 2:
            return False
        d = dictview_dict(self)
        found = find_value(d, item[0], key_hash(item[0]))
        return found is not MISSING and (found is item[1] or found == item[1])

    def __lt__(self, other):
        return view_compare(self, other, 0)

    def __le__(self, other):
        return view_compare(self, other, 1)

    def __eq__(self, other):
        return view_compare(self, other, 2)

    def __ne__(self, other):
        return view_compare(self, other, 3)

    def __gt__(self, other):
        return view_compare(self, other, 4)

    def __ge__(self, other):
        return view_compare(self, other, 5)

    __hash__ = None

    def __sub__(self, other):
        return view_sub(self, other)

    def __rsub__(self, other):
        return view_sub(other, self)

    def __and__(self, other):
        return view_and(self, other)

    def __rand__(self, other):
        return view_and(other, self)

    def __xor__(self, other):
        return view_xor(self, other)

    def __rxor__(self, other):
        return view_xor(other, self)

    def __or__(self, other):
        return view_or(self, other)

    def __ror__(self, other):
        return view_or(other, self)

    def isdisjoint(self, other):
        """Return True if the view and the given iterable have a null intersection."""
        return view_isdisjoint(self, other)


@kernel_type(static_type('dict_values'))
class dict_values:
    def __len__(self):
        return size(dictview_dict(self))

    def __repr__(self):
        return view_repr(self)

    def __iter__(self):
        return dictiter_new(static_type('dict_valueiterator'), dictview_dict(self))

    def __reversed__(self):
        """Return a reverse iterator over the dict values."""
        return dictiter_new(static_type('dict_reversevalueiterator'), dictview_dict(self))


kernel_set('build_map', build_map)
kernel_set('dict_get', _get)
kernel_set('dict_items', _items)
kernel_set('dict_setitem', setitem)
kernel_set('dict_delitem', delitem)
kernel_set('dict_update', update_op)
kernel_set('dict_merge', merge_kwargs)
