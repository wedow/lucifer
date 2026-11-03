"""The cell type (Objects/cellobject.c): the closure cells of functions."""

from _lucifer import MISSING, str_join, kind, static_type, address, alloc, cell_get, cell_set
from _lucifer import _boot

kernel_type = _boot.kernel_type
getset = _boot.getset

__all__ = ()


def _get(self):
    value = cell_get(self)
    if value is MISSING:
        raise ValueError('Cell is empty')
    return value


def _set(self, value):
    cell_set(self, value)


def _compare(self, other, op):
    """cell_richcompare: empty cells sort before every full cell."""
    if not kind(other, 'cell'):
        return NotImplemented
    a, b = cell_get(self), cell_get(other)
    if a is not MISSING and b is not MISSING:
        return (a < b, a <= b, a == b, a != b, a > b, a >= b)[op]
    full_a, full_b = a is not MISSING, b is not MISSING
    return (full_a < full_b, full_a <= full_b, full_a == full_b, full_a != full_b,
            full_a > full_b, full_a >= full_b)[op]


@kernel_type(static_type('cell'))
class cell:
    """Create a new cell object.

  contents
    the contents of the cell. If not specified, the cell will be empty,
    and further attempts to access its cell_contents attribute will
    raise a ValueError."""

    def __new__(cls, contents=MISSING, /):
        self = alloc(cls, cell)
        cell_set(self, contents)
        return self

    def __repr__(self):
        value = cell_get(self)
        if value is MISSING:
            return str_join('', ('<cell at ', address(self), ': empty>'))
        return str_join('', ('<cell at ', address(self), ': ', type(value).__name__,
                             ' object at ', address(value), '>'))

    def __lt__(self, other):
        return _compare(self, other, 0)

    def __le__(self, other):
        return _compare(self, other, 1)

    def __eq__(self, other):
        return _compare(self, other, 2)

    def __ne__(self, other):
        return _compare(self, other, 3)

    def __gt__(self, other):
        return _compare(self, other, 4)

    def __ge__(self, other):
        return _compare(self, other, 5)

    __hash__ = None
    cell_contents = getset(_get, _set)
