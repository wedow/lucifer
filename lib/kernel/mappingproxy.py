"""mappingproxy (Objects/descrobject.c): a read-only view of a mapping that
passes every read through. The layout is native (runtime/object/descr.c);
the mapping is its one field. The dict views hand out theirs as one."""

from _lucifer import (
    MISSING, error, str_join, kind, type_lookup, static_type, field_get, proxy_new, dictview_dict,
)
from _lucifer import _boot, _args

kernel_type = _boot.kernel_type
getset = _boot.getset

__all__ = ()

_MAPPING = 0
_Proxy = static_type('mappingproxy')


def _check_mapping(mapping):
    """mappingproxy_check_mapping: PyMapping_Check, and not a list or tuple."""
    if (type_lookup(type(mapping), '__getitem__') is MISSING
            or kind(mapping, 'list') or kind(mapping, 'tuple')):
        error(TypeError, str_join('', (
            'mappingproxy() argument must be a mapping, not ', type(mapping).__name__)))


def _unwrap(x):
    return field_get(x, _MAPPING) if type(x) is _Proxy else x


@kernel_type(_Proxy)
class mappingproxy:
    """Read-only proxy of a mapping."""

    def __new__(cls, /, *args, **kwargs):
        mapping, = _args.unpack('mappingproxy', args, kwargs, ('mapping',), 0, 1)
        _check_mapping(mapping)
        return proxy_new(mapping)

    def __getitem__(self, key):
        return field_get(self, _MAPPING)[key]

    def __contains__(self, key):
        return key in field_get(self, _MAPPING)

    def __len__(self):
        return len(field_get(self, _MAPPING))

    def __iter__(self):
        return iter(field_get(self, _MAPPING))

    def __reversed__(self):
        """D.__reversed__() -> reverse iterator"""
        return field_get(self, _MAPPING).__reversed__()

    def get(self, key, default=None, /):
        """Return the value for key if key is in the mapping, else default."""
        return field_get(self, _MAPPING).get(key, default)

    def keys(self):
        """D.keys() -> a set-like object providing a view on D's keys"""
        return field_get(self, _MAPPING).keys()

    def values(self):
        """D.values() -> an object providing a view on D's values"""
        return field_get(self, _MAPPING).values()

    def items(self):
        """D.items() -> a set-like object providing a view on D's items"""
        return field_get(self, _MAPPING).items()

    def copy(self):
        """D.copy() -> a shallow copy of D"""
        return field_get(self, _MAPPING).copy()

    def __repr__(self):
        return str_join('', ('mappingproxy(', repr(field_get(self, _MAPPING)), ')'))

    def __str__(self):
        return str(field_get(self, _MAPPING))

    def __hash__(self):
        return hash(field_get(self, _MAPPING))

    def __eq__(self, other):
        return field_get(self, _MAPPING) == other

    def __ne__(self, other):
        return field_get(self, _MAPPING) != other

    def __lt__(self, other):
        return field_get(self, _MAPPING) < other

    def __le__(self, other):
        return field_get(self, _MAPPING) <= other

    def __gt__(self, other):
        return field_get(self, _MAPPING) > other

    def __ge__(self, other):
        return field_get(self, _MAPPING) >= other

    def __or__(self, other):
        return _unwrap(self) | _unwrap(other)

    def __ror__(self, other):
        return _unwrap(other) | _unwrap(self)

    def __ior__(self, other):
        error(TypeError, "'|=' is not supported by mappingproxy; use '|' instead")


def _view_mapping(self):
    """dictionary that this view refers to"""
    return proxy_new(dictview_dict(self))


for _name in ('dict_keys', 'dict_values', 'dict_items'):
    kernel_type(static_type(_name))(('', (), {'mapping': getset(_view_mapping)}))
