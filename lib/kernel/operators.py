"""The generic paths of the operator opcodes (Objects/abstract.c): binary
operations with their reflection, rich comparison, unary operators, truth,
containment and PyNumber_Index. Each looks the special method up on the
type, as the slots' thunks do for C callers; the interpreter pushes these
functions' frames, so reflection never recurses in C (DESIGN 5.2)."""

from _lucifer import (
    MISSING, error, str_join, kind, type_lookup, type_tp_name, is_subtype, kernel_set, int_add,
    truth_exact, int_format, int_hash, str_hash, size, type_has_sequence, list_append, new_check, kernel_get,
)
from _lucifer import _boot

call_special = _boot.call_special

__all__ = ()

# BINARY_OP's operators in oparg order (opcode.h NB_*): the method, its
# reflection, its in-place form and the symbol of the error message.
_BINARY = (
    ('__add__', '__radd__', '__iadd__', '+'),
    ('__and__', '__rand__', '__iand__', '&'),
    ('__floordiv__', '__rfloordiv__', '__ifloordiv__', '//'),
    ('__lshift__', '__rlshift__', '__ilshift__', '<<'),
    ('__matmul__', '__rmatmul__', '__imatmul__', '@'),
    ('__mul__', '__rmul__', '__imul__', '*'),
    ('__mod__', '__rmod__', '__imod__', '%'),
    ('__or__', '__ror__', '__ior__', '|'),
    ('__pow__', '__rpow__', '__ipow__', '**'),
    ('__rshift__', '__rrshift__', '__irshift__', '>>'),
    ('__sub__', '__rsub__', '__isub__', '-'),
    ('__truediv__', '__rtruediv__', '__itruediv__', '/'),
    ('__xor__', '__rxor__', '__ixor__', '^'),
)

# Py_LT .. Py_GE: the method, the reflected method and the symbol.
_COMPARE = (
    ('__lt__', '__gt__', '<'),
    ('__le__', '__ge__', '<='),
    ('__eq__', '__eq__', '=='),
    ('__ne__', '__ne__', '!='),
    ('__gt__', '__lt__', '>'),
    ('__ge__', '__le__', '>='),
)


# Types whose __add__, __mul__ and __rmul__ are sequence slots (sq_concat,
# sq_repeat). A method found on an operand's type that is one of theirs runs
# only after the number slots of both operands have declined (PyNumber_Add,
# PyNumber_Multiply), so a user __radd__ or __rmul__ wins, and a subclass that
# overrides the method has a number slot instead.
_SEQUENCES = []


def add_sequence(t):
    list_append(_SEQUENCES, t)


def _sequence_slot(t, name):
    """The function of t's method name when it is a sequence slot, else MISSING."""
    f = type_lookup(t, name)
    for s in _SEQUENCES:
        if f is type_lookup(s, name):
            return f
    return MISSING


def _entry(table, i):
    for e in table:
        if i == 0:
            return e
        i -= 1


def _name(x):
    return type_tp_name(type(x))


def _unsupported(sym, a, b):
    error(TypeError, str_join('', (
        'unsupported operand type(s) for ', sym, ": '", _name(a), "' and '", _name(b), "'")))


def binary_op(a, b, op):
    """PyNumber_<op>(a, b) and PyNumber_InPlace<op>(a, b) for BINARY_OP's oparg."""
    inplace = op >= 13
    forward, reverse, iname, sym = _entry(_BINARY, op - 13 if inplace else op)
    ta = type(a)
    if inplace:
        f = type_lookup(ta, iname)
        if f is not MISSING:
            r = call_special(f, a, b)
            if r is not NotImplemented:
                return r
        sym = str_join('', (sym, '='))
    elif op == 8:
        sym = '** or pow()'
    tb = type(b)
    fb = MISSING
    seq_a = seq_b = MISSING
    if tb is not ta:
        fb = type_lookup(tb, reverse)
    f = type_lookup(ta, forward)
    if op == 0 or op == 13:
        seq_a = _sequence_slot(ta, forward)
    elif op == 5 or op == 18:
        seq_a = _sequence_slot(ta, forward)
        seq_b = _sequence_slot(tb, reverse)
    if f is seq_a:
        f = MISSING
    if fb is seq_b:
        fb = MISSING
    # A subclass's own reflected method goes first (method_is_overloaded).
    if fb is not MISSING and is_subtype(tb, ta) and fb is not type_lookup(ta, reverse):
        r = call_special(fb, b, a)
        if r is not NotImplemented:
            return r
        fb = MISSING
    if f is not MISSING:
        r = call_special(f, a, b)
        if r is not NotImplemented:
            return r
    if fb is not MISSING:
        r = call_special(fb, b, a)
        if r is not NotImplemented:
            return r
    if seq_a is not MISSING:
        return call_special(seq_a, a, b)
    if seq_b is not MISSING:
        return call_special(seq_b, b, a)
    _unsupported(sym, a, b)


def truth(v):
    """PyObject_IsTrue as a bool: __bool__, then __len__, else true."""
    r = truth_exact(v)
    if r is not MISSING:
        return r
    f = type_lookup(type(v), '__bool__')
    if f is not MISSING:
        r = call_special(f, v)
        if r is True or r is False:
            return r
        error(TypeError, str_join('', ('__bool__ should return bool, returned ', _name(r))))
    f = type_lookup(type(v), '__len__')
    if f is not MISSING:
        n = index(call_special(f, v))
        if n < 0:
            error(ValueError, '__len__() should return >= 0')
        return n != 0
    return True


def compare(a, b, oparg):
    """COMPARE_OP: PyObject_RichCompare; oparg bit 4 asks for a bool."""
    forward, reverse, sym = _entry(_COMPARE, oparg >> 5)
    ta = type(a)
    tb = type(b)
    done = False
    r = NotImplemented
    if tb is not ta and is_subtype(tb, ta):
        f = type_lookup(tb, reverse)
        if f is not MISSING:
            r = call_special(f, b, a)
        done = True
    if r is NotImplemented:
        f = type_lookup(ta, forward)
        if f is not MISSING:
            r = call_special(f, a, b)
    if r is NotImplemented and not done:
        f = type_lookup(tb, reverse)
        if f is not MISSING:
            r = call_special(f, b, a)
    if r is NotImplemented:
        op = oparg >> 5
        if op == 2:
            r = a is b
        elif op == 3:
            r = a is not b
        else:
            error(TypeError, str_join('', (
                "'", sym, "' not supported between instances of '", _name(a), "' and '",
                _name(b), "'")))
    if oparg & 16:
        return truth(r)
    return r


def _unary(v, name, sym):
    f = type_lookup(type(v), name)
    if f is MISSING:
        error(TypeError, str_join('', ('bad operand type for unary ', sym, ": '", _name(v), "'")))
    return call_special(f, v)


def negative(v):
    return _unary(v, '__neg__', '-')


def positive(v):
    return _unary(v, '__pos__', '+')


def invert(v):
    return _unary(v, '__invert__', '~')


def index(x):
    """PyNumber_Index: x as an exact int, through __index__."""
    if kind(x, 'int'):
        return x if type(x) is int else int_add(x, 0)
    f = type_lookup(type(x), '__index__')
    if f is MISSING:
        error(TypeError, str_join('', ("'", _name(x), "' object cannot be interpreted as an integer")))
    r = call_special(f, x)
    if not kind(r, 'int'):
        error(TypeError, str_join('', ('__index__ returned non-int (type ', _name(r), ')')))
    return r if type(r) is int else int_add(r, 0)


kernel_set('binary_op', binary_op)
kernel_set('compare', compare)
kernel_set('negative', negative)
kernel_set('positive', positive)
kernel_set('invert', invert)
kernel_set('truth', truth)


_SSIZE_MAX = 9223372036854775807
_SSIZE_MIN = -9223372036854775808


def check_new(cls, owner):
    """tp_new_wrapper's checks of owner.__new__(cls)."""
    if not kind(cls, 'type'):
        error(TypeError, str_join('', (
            type_tp_name(owner), '.__new__(X): X is not a type object (', _name(cls), ')')))
    if cls is not owner:
        new_check(cls, owner)


def decimal(n):
    return int_format(n, 10, False)


def as_ssize(x, exc):
    """PyNumber_AsSsize_t: x through __index__, as a machine-sized int; one
    that does not fit is clipped, or raises exc when exc is not None."""
    n = x if type(x) is int else index(x)
    if n > _SSIZE_MAX or n < _SSIZE_MIN:
        if exc is not None:
            error(exc, "cannot fit 'int' into an index-sized integer")
        return _SSIZE_MAX if n > 0 else _SSIZE_MIN
    return n


def hash_of(x):
    """PyObject_Hash, as an int that fits Py_hash_t."""
    t = type(x)
    if t is str:
        return str_hash(x)
    if t is int:
        return int_hash(x)
    f = type_lookup(type(x), '__hash__')
    if f is MISSING or f is None:
        error(TypeError, str_join('', ("unhashable type: '", _name(x), "'")))
    h = call_special(f, x)
    if not kind(h, 'int'):
        error(TypeError, '__hash__ method should return an integer')
    h = index(h)
    if h > _SSIZE_MAX or h < _SSIZE_MIN:
        h = int_hash(h)
    return -2 if h == -1 else h


def length(obj):
    """PyObject_Size: len(obj), through __len__."""
    t = type(obj)
    if t is list or t is tuple or t is dict or t is str or t is set or t is frozenset or t is bytes:
        return size(obj)
    f = type_lookup(type(obj), '__len__')
    if f is MISSING:
        error(TypeError, str_join('', ("object of type '", _name(obj), "' has no len()")))
    n = as_ssize(call_special(f, obj), OverflowError)
    if n < 0:
        error(ValueError, '__len__() should return >= 0')
    return n


def getitem(obj, key):
    """PyObject_GetItem: obj[key], which a class answers with __class_getitem__."""
    f = type_lookup(type(obj), '__getitem__')
    if f is not MISSING:
        return call_special(f, obj, key)
    if kind(obj, 'type'):
        if obj is type:
            return kernel_get('generic_alias')(obj, key)
        try:
            meth = obj.__class_getitem__
        except AttributeError:
            meth = None
        if meth is not None:
            return meth(key)
        error(TypeError, str_join('', ("type '", type_tp_name(obj), "' is not subscriptable")))
    error(TypeError, str_join('', ("'", _name(obj), "' object is not subscriptable")))


def setitem(obj, key, value):
    """PyObject_SetItem: obj[key] = value."""
    f = type_lookup(type(obj), '__setitem__')
    if f is MISSING:
        error(TypeError, str_join('', ("'", _name(obj), "' object does not support item assignment")))
    call_special(f, obj, key, value)


def delitem(obj, key):
    """PyObject_DelItem: del obj[key]. A type with sequence slots words the
    refusal of an index key as PySequence_DelItem does."""
    f = type_lookup(type(obj), '__delitem__')
    if f is not MISSING:
        call_special(f, obj, key)
        return
    if type_has_sequence(type(obj)) and type_lookup(type(key), '__index__') is not MISSING:
        as_ssize(key, IndexError)
        error(TypeError, str_join('', ("'", _name(obj), "' object doesn't support item deletion")))
    error(TypeError, str_join('', ("'", _name(obj), "' object does not support item deletion")))


kernel_set('getitem', getitem)
kernel_set('setitem', setitem)
kernel_set('delitem', delitem)
kernel_set('length', length)
