"""Class creation and the type installer (DESIGN 4.4).

__build_class__, the slotdefs table of Objects/typeobject.c, the C3 MRO and
@kernel_type, which installs a kernel class body into a static type. This
module runs before object and type exist, so it holds functions only and
reaches objects through _lucifer primitives, never through attributes.

In kernel modules a class statement does not make a class: it yields its
body, (name, bases, namespace), which @kernel_type(T) installs into T.
Plain functions become method descriptors, dunders with a slot become slot
wrappers, __new__ a builtin bound to T; @classmethod_ and getset() mark the
other kinds.
"""

from _lucifer import (
    MISSING, error, str_join, ns_get, ns_set, ns_del, ns_items, kind, size,
    type_lookup, type_dict, type_mro, type_bases, type_base, type_tp_name,
    is_subtype, make_function, make_getset, set_slot, type_set_bases,
    type_set_mro, call_with_locals, cell_get, static_type, new_check,
)

__all__ = ('__build_class__',)

_function = static_type('function')
_wrapper_descriptor = static_type('wrapper_descriptor')
_method_descriptor = static_type('method_descriptor')

# slotdefs: each dunder and the C slot it feeds. Several dunders can share
# a slot (__getattribute__ and __getattr__, the rich comparisons); __add__
# and the like feed both the number and the sequence slot.
SLOTDEFS = (
    ('__getattribute__', 'tp_getattro'), ('__getattr__', 'tp_getattro'),
    ('__setattr__', 'tp_setattro'), ('__delattr__', 'tp_setattro'),
    ('__repr__', 'tp_repr'), ('__hash__', 'tp_hash'), ('__call__', 'tp_call'),
    ('__str__', 'tp_str'),
    ('__lt__', 'tp_richcompare'), ('__le__', 'tp_richcompare'), ('__eq__', 'tp_richcompare'),
    ('__ne__', 'tp_richcompare'), ('__gt__', 'tp_richcompare'), ('__ge__', 'tp_richcompare'),
    ('__iter__', 'tp_iter'), ('__next__', 'tp_iternext'),
    ('__get__', 'tp_descr_get'), ('__set__', 'tp_descr_set'), ('__delete__', 'tp_descr_set'),
    ('__init__', 'tp_init'), ('__new__', 'tp_new'), ('__del__', 'tp_finalize'),
    ('__buffer__', 'bf_getbuffer'), ('__release_buffer__', 'bf_releasebuffer'),
    ('__await__', 'am_await'), ('__aiter__', 'am_aiter'), ('__anext__', 'am_anext'),
    ('__add__', 'nb_add'), ('__radd__', 'nb_add'), ('__sub__', 'nb_subtract'),
    ('__rsub__', 'nb_subtract'), ('__mul__', 'nb_multiply'), ('__rmul__', 'nb_multiply'),
    ('__mod__', 'nb_remainder'), ('__rmod__', 'nb_remainder'), ('__divmod__', 'nb_divmod'),
    ('__rdivmod__', 'nb_divmod'), ('__pow__', 'nb_power'), ('__rpow__', 'nb_power'),
    ('__neg__', 'nb_negative'), ('__pos__', 'nb_positive'), ('__abs__', 'nb_absolute'),
    ('__bool__', 'nb_bool'), ('__invert__', 'nb_invert'), ('__lshift__', 'nb_lshift'),
    ('__rlshift__', 'nb_lshift'), ('__rshift__', 'nb_rshift'), ('__rrshift__', 'nb_rshift'),
    ('__and__', 'nb_and'), ('__rand__', 'nb_and'), ('__xor__', 'nb_xor'),
    ('__rxor__', 'nb_xor'), ('__or__', 'nb_or'), ('__ror__', 'nb_or'), ('__int__', 'nb_int'),
    ('__float__', 'nb_float'), ('__iadd__', 'nb_inplace_add'),
    ('__isub__', 'nb_inplace_subtract'), ('__imul__', 'nb_inplace_multiply'),
    ('__imod__', 'nb_inplace_remainder'), ('__ipow__', 'nb_inplace_power'),
    ('__ilshift__', 'nb_inplace_lshift'), ('__irshift__', 'nb_inplace_rshift'),
    ('__iand__', 'nb_inplace_and'), ('__ixor__', 'nb_inplace_xor'),
    ('__ior__', 'nb_inplace_or'), ('__floordiv__', 'nb_floor_divide'),
    ('__rfloordiv__', 'nb_floor_divide'), ('__truediv__', 'nb_true_divide'),
    ('__rtruediv__', 'nb_true_divide'), ('__ifloordiv__', 'nb_inplace_floor_divide'),
    ('__itruediv__', 'nb_inplace_true_divide'), ('__index__', 'nb_index'),
    ('__matmul__', 'nb_matrix_multiply'), ('__rmatmul__', 'nb_matrix_multiply'),
    ('__imatmul__', 'nb_inplace_matrix_multiply'),
    ('__len__', 'mp_length'), ('__getitem__', 'mp_subscript'),
    ('__setitem__', 'mp_ass_subscript'), ('__delitem__', 'mp_ass_subscript'),
    ('__len__', 'sq_length'), ('__add__', 'sq_concat'), ('__mul__', 'sq_repeat'),
    ('__rmul__', 'sq_repeat'), ('__getitem__', 'sq_item'), ('__setitem__', 'sq_ass_item'),
    ('__delitem__', 'sq_ass_item'), ('__contains__', 'sq_contains'),
    ('__iadd__', 'sq_inplace_concat'), ('__imul__', 'sq_inplace_repeat'),
)


def _group_slots():
    groups = {}
    dunders = {}
    for dunder, slot in SLOTDEFS:
        ns_set(groups, slot, (*ns_get(groups, slot, ()), dunder))
        ns_set(dunders, dunder, True)
    return ns_items(groups), dunders


SLOTS, SLOT_DUNDERS = _group_slots()

# Names a class body holds that a static type does not keep, unless the
# body defines them as descriptors.
_BODY_ONLY = {'__module__': 0, '__qualname__': 0, '__firstlineno__': 0,
              '__static_attributes__': 0, '__classcell__': 0, '__classdictcell__': 0}

# Slot settings for set_slot.
_CLEAR, _THUNK, _INHERIT, _HASH_NOT_IMPLEMENTED = 0, 1, 2, 3


def update_slots(cls, own_only):
    """fixup_slot_dispatchers: each slot gets the thunk when one of its
    dunders is defined (in cls's own dict for a static type, anywhere in its
    MRO for a class) and stays inherited otherwise; __hash__ = None makes
    the type unhashable."""
    d = type_dict(cls)
    base = type_base(cls)
    for slot, names in SLOTS:
        found = MISSING
        for name in names:
            v = ns_get(d, name, MISSING) if own_only else type_lookup(cls, name)
            if v is not MISSING:
                found = v
        if found is MISSING:
            if not own_only:
                set_slot(cls, slot, _INHERIT, base)
        elif found is None and slot == 'tp_hash':
            set_slot(cls, slot, _HASH_NOT_IMPLEMENTED, None)
        else:
            set_slot(cls, slot, _THUNK, None)


def check_new(cls, owner):
    """tp_new_wrapper's checks of owner.__new__(cls)."""
    if not kind(cls, 'type'):
        error(TypeError, str_join('', (
            type_tp_name(owner), '.__new__(X): X is not a type object (',
            type_tp_name(type(cls)), ')')))
    if cls is not owner:
        new_check(cls, owner)


def classmethod_(func):
    """In a kernel class body: install func as a classmethod descriptor."""
    return (classmethod_, func, None)


def getset(fget, fset=None):
    """In a kernel class body: a getset descriptor over two kernel functions;
    fset receives MISSING for a deletion."""
    return (getset, fget, fset)


def _kernel_value(cls, name, value):
    if kind(value, 'function'):
        if name == '__new__':
            return make_function(cls, name, value, 'new')
        if ns_get(SLOT_DUNDERS, name, False):
            return make_function(cls, name, value, 'wrapper')
        return make_function(cls, name, value, 'method')
    if kind(value, 'tuple') and size(value) == 3:
        tag, a, b = value
        if tag is classmethod_:
            return make_function(cls, name, a, 'classmethod')
        if tag is getset:
            return make_getset(cls, name, a, b)
    return value


def kernel_type(cls):
    """@kernel_type(T) on a kernel class statement installs its body into
    the static type T, whose dict lives in interpreter state."""
    def install(body):
        name, bases, ns = body
        if bases:
            type_set_bases(cls, bases)
            type_set_mro(cls, mro_of(cls))
        d = type_dict(cls)
        for key, value in ns_items(ns):
            if key == '__doc__' and ns_get(d, key, None) is not None:
                continue            # the layout's own __doc__ member
            if ns_get(_BODY_ONLY, key, MISSING) is MISSING or kind(value, 'tuple'):
                ns_set(d, key, _kernel_value(cls, key, value))
        update_slots(cls, True)
        return cls
    return install


def _names(types):
    return str_join(', ', (*[type_tp_name(t) for t in types],))


def _first(seq):
    for x in seq:
        return x


def _in_tail(candidate, seqs):
    for seq in seqs:
        head = True
        for x in seq:
            if head:
                head = False
            elif x is candidate:
                return True
    return False


def mro_of(cls):
    """mro_implementation: C3 over the bases' MROs and the bases."""
    bases = type_bases(cls)
    seen = []
    for base in bases:
        for other in seen:
            if other is base:
                error(TypeError, str_join('', ('duplicate base class ', type_tp_name(base))))
        seen = [*seen, base]
    if size(bases) == 1:
        return (cls, *type_mro(_first(bases)))
    seqs = [[*type_mro(base)] for base in bases]
    seqs = [*seqs, [*bases]]
    result = [cls]
    while True:
        seqs = [seq for seq in seqs if seq]
        if not seqs:
            return (*result,)
        winner = MISSING
        for seq in seqs:
            head = _first(seq)
            if not _in_tail(head, seqs):
                winner = head
                break
        if winner is MISSING:
            heads = []
            for seq in seqs:
                head = _first(seq)
                if not [h for h in heads if h is head]:
                    heads = [*heads, head]
            error(TypeError, str_join('', (
                'Cannot create a consistent method resolution order (MRO) for bases ',
                _names(heads))))
        result = [*result, winner]
        seqs = [[x for x in seq if x is not winner] for seq in seqs]


def calculate_metaclass(meta, bases):
    """_PyType_CalculateMetaclass: the most derived metaclass."""
    winner = meta
    for base in bases:
        t = type(base)
        if is_subtype(winner, t):
            continue
        if is_subtype(t, winner):
            winner = t
            continue
        error(TypeError, 'metaclass conflict: the metaclass of a derived class must be a '
                         '(non-strict) subclass of the metaclasses of all its bases')
    return winner


def is_data(descr):
    """Whether descr's type defines __set__ or __delete__ (tp_descr_set)."""
    t = type(descr)
    return type_lookup(t, '__set__') is not MISSING or type_lookup(t, '__delete__') is not MISSING


def call_special(descr, obj, /, *args, **kwargs):
    """Calls a special method found on obj's type (call_attribute): functions
    and kernel descriptors unbound, anything else bound through __get__."""
    t = type(descr)
    if t is _function or t is _wrapper_descriptor or t is _method_descriptor:
        return descr(obj, *args, **kwargs)
    get = type_lookup(t, '__get__')
    if get is not MISSING:
        descr = get(descr, obj, type(obj))
    return descr(*args, **kwargs)


def _update_bases(bases):
    """update_bases: __mro_entries__ of non-class bases."""
    changed = False
    result = []
    for base in bases:
        if kind(base, 'type') or type_lookup(type(base), '__mro_entries__') is MISSING:
            result = [*result, base]
            continue
        entries = base.__mro_entries__(bases)
        if not kind(entries, 'tuple'):
            error(TypeError, '__mro_entries__ must return a tuple')
        result = [*result, *entries]
        changed = True
    return (*result,) if changed else bases


def __build_class__(func, name, /, *bases, **kwds):
    """__build_class__(func, name, /, *bases, [metaclass], **kwds) -> class"""
    if not kind(func, 'function'):
        error(TypeError, '__build_class__: func must be a function')
    if not kind(name, 'str'):
        error(TypeError, '__build_class__: name is not a string')
    orig_bases = bases
    bases = _update_bases(bases)
    if kind(func, 'kernel'):
        ns = {}
        call_with_locals(func, ns)
        return (name, bases, ns)
    meta = ns_get(kwds, 'metaclass', MISSING)
    if meta is MISSING:
        meta = type(_first(bases)) if bases else type
        isclass = True
    else:
        ns_del(kwds, 'metaclass')
        isclass = kind(meta, 'type')
    if isclass:
        meta = calculate_metaclass(meta, bases)
    if isclass or type_lookup(type(meta), '__prepare__') is not MISSING:
        ns = meta.__prepare__(name, bases, **kwds)
    else:
        ns = {}
    if not kind(ns, 'dict'):
        error(TypeError, '__prepare__() must return a mapping (a dict until K1j)')
    cell = call_with_locals(func, ns)
    if bases is not orig_bases:
        ns_set(ns, '__orig_bases__', orig_bases)
    cls = meta(name, bases, ns, **kwds)
    if kind(cls, 'type') and kind(cell, 'cell'):
        cell_cls = cell_get(cell)
        if cell_cls is not cls:
            if cell_cls is MISSING:
                error(RuntimeError, str_join('', (
                    "__class__ not set defining '", name, "' as ", type.__repr__(cls),
                    '. Was __classcell__ propagated to type.__new__?')))
            error(TypeError, str_join('', (
                '__class__ set to ', type.__repr__(cell_cls), " defining '", name, "' as ",
                type.__repr__(cls))))
    return cls
