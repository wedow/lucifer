"""The pattern matching operations (Python/ceval.c _PyEval_MatchClass,
match_keys and the collection flags of Objects/typeobject.c). A type is a
mapping or a sequence for patterns when it is the builtin one, derives from
it or is registered with the collections.abc class, which is how CPython sets
Py_TPFLAGS_MAPPING and Py_TPFLAGS_SEQUENCE."""

from _lucifer import kernel_set
import sys

__all__ = ()

_MATCH_SELF = (bool, bytearray, bytes, dict, float, frozenset, int, list, set, str, tuple)
_NOT_SEQUENCES = (str, bytes, bytearray)


def _abc(name):
    module = sys.modules.get('_collections_abc')
    return None if module is None else getattr(module, name)


_TPFLAGS_SEQUENCE = 1 << 5      # what an extension type sets to match as a sequence
_TPFLAGS_MAPPING = 1 << 6


def match_mapping(subject):
    if isinstance(subject, dict) or type(subject).__flags__ & _TPFLAGS_MAPPING:
        return True
    abc = _abc('Mapping')
    return abc is not None and isinstance(subject, abc)


def match_sequence(subject):
    if isinstance(subject, _NOT_SEQUENCES):
        return False
    if isinstance(subject, (list, tuple, range, memoryview)) or type(subject).__flags__ & _TPFLAGS_SEQUENCE:
        return True
    deque = getattr(sys.modules.get('_collections'), 'deque', None)    # a native sequence in CPython
    if deque is not None and isinstance(subject, deque):
        return True
    abc = _abc('Sequence')
    return abc is not None and isinstance(subject, abc)


_MISSING = object()


def match_keys(subject, keys):
    seen = set()
    values = []
    get = subject.get
    for key in keys:
        if key in seen:
            raise ValueError('mapping pattern checks duplicate key (%r)' % (key,))
        seen.add(key)
        value = get(key, _MISSING)
        if value is _MISSING:
            return None
        values.append(value)
    return tuple(values)


def _class_attr(subject, cls, name, seen):
    if name in seen:
        raise TypeError('%s() got multiple sub-patterns for attribute %r' % (cls.__name__, name))
    seen.append(name)
    try:
        return getattr(subject, name)
    except AttributeError:
        return _MISSING


def match_class(subject, cls, names, nargs):
    if not isinstance(cls, type):
        raise TypeError('called match pattern must be a class')
    if not isinstance(names, tuple):
        raise TypeError('kwarg names must be a tuple')
    if not isinstance(subject, cls):
        return None
    seen = []
    attrs = []
    if nargs:
        match_self = False
        try:
            match_args = cls.__match_args__
        except AttributeError:
            match_args = ()
            match_self = issubclass(cls, _MATCH_SELF)
        else:
            if not isinstance(match_args, tuple):
                raise TypeError('%s.__match_args__ must be a tuple (got %s)'
                                % (cls.__name__, type(match_args).__name__))
        allowed = 1 if match_self else len(match_args)
        if allowed < nargs:
            raise TypeError('%s() accepts %d positional sub-pattern%s (%d given)'
                            % (cls.__name__, allowed, '' if allowed == 1 else 's', nargs))
        if match_self:
            attrs.append(subject)
        else:
            for i in range(nargs):
                name = match_args[i]
                if not isinstance(name, str):
                    raise TypeError('__match_args__ elements must be strings (got %s)' % type(name).__name__)
                attr = _class_attr(subject, cls, name, seen)
                if attr is _MISSING:
                    return None
                attrs.append(attr)
    for name in names:
        attr = _class_attr(subject, cls, name, seen)
        if attr is _MISSING:
            return None
        attrs.append(attr)
    return tuple(attrs)


kernel_set('match_mapping', match_mapping)
kernel_set('match_sequence', match_sequence)
kernel_set('match_keys', match_keys)
kernel_set('match_class', match_class)
