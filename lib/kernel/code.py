"""The code type's methods (Objects/codeobject.c). The code objects, their
attributes and construction are native (runtime/interp/code.c); equality is
what the compiler's constant cache needs: two code objects with the same
bytecode, constants and tables are interchangeable."""

from _lucifer import str_join, static_type, address, code_equal, code_hash, code_new, code_localsplus, MISSING
from _lucifer import _boot

kernel_type = _boot.kernel_type

__all__ = ()

_code = static_type('code')


@kernel_type(_code)
class code:
    def __eq__(self, other):
        if type(other) is not _code:
            return NotImplemented
        return code_equal(self, other)

    def __ne__(self, other):
        if type(other) is not _code:
            return NotImplemented
        return not code_equal(self, other)

    def __hash__(self):
        return code_hash(self)

    def __repr__(self):
        return str_join('', ('<code object ', self.co_name, ' at ', address(self), ', file "',
                             self.co_filename, '", line ', str(self.co_firstlineno), '>'))

    def replace(self, **changes):
        """Return a copy of the code object with new values for the specified fields.

The local variable names (co_varnames, co_cellvars, co_freevars, co_nlocals) are those
the code was compiled with."""
        fields = {
            'co_argcount': self.co_argcount, 'co_posonlyargcount': self.co_posonlyargcount,
            'co_kwonlyargcount': self.co_kwonlyargcount, 'co_stacksize': self.co_stacksize,
            'co_flags': self.co_flags, 'co_firstlineno': self.co_firstlineno, 'co_code': self.co_code,
            'co_consts': self.co_consts, 'co_names': self.co_names, 'co_filename': self.co_filename,
            'co_name': self.co_name, 'co_qualname': self.co_qualname, 'co_linetable': self.co_linetable,
            'co_exceptiontable': self.co_exceptiontable}
        for name, value in changes.items():
            if name in fields:
                fields[name] = value
            elif name in ('co_nlocals', 'co_varnames', 'co_cellvars', 'co_freevars'):
                if value != getattr(self, name):
                    raise NotImplementedError('code.replace(%s=...) is not supported' % name)
            else:
                raise TypeError("replace() got an unexpected keyword argument '%s'" % name)
        names, kinds = code_localsplus(self)
        return code_new(fields['co_filename'], fields['co_name'], fields['co_qualname'], fields['co_flags'],
                        fields['co_code'], fields['co_firstlineno'], fields['co_linetable'],
                        fields['co_consts'], fields['co_names'], names, kinds, fields['co_argcount'],
                        fields['co_posonlyargcount'], fields['co_kwonlyargcount'],
                        fields['co_stacksize'], fields['co_exceptiontable'])

    def __replace__(self, **changes):
        return self.replace(**changes)

    def co_positions(self):
        """Return an iterator over the source code positions of each bytecode instruction."""
        return _positions(self.co_linetable, self.co_firstlineno)

    def co_lines(self):
        """Return an iterator that produces (start, end, lineno) for each line range."""
        return _lines(self.co_linetable, self.co_firstlineno)


def _varint(table, i):
    shift = 0
    value = 0
    while True:
        b = table[i]
        i += 1
        value |= (b & 63) << shift
        shift += 6
        if not b & 64:
            return value, i


def _svarint(table, i):
    value, i = _varint(table, i)
    return (-(value >> 1) if value & 1 else value >> 1), i


def _entries(table, firstlineno):
    """The location table's entries: (length in code units, line, end line,
    column, end column), line -1 for no location and columns None when absent."""
    i = 0
    line = firstlineno
    n = len(table)
    while i < n:
        first = table[i]
        i += 1
        kind = (first >> 3) & 15
        length = (first & 7) + 1
        if kind == 15:
            yield length, None, None, None, None
            while i < n and not table[i] & 128:
                i += 1
            continue
        if kind == 14:
            delta, i = _svarint(table, i)
            line += delta
            extra, i = _varint(table, i)
            col, i = _varint(table, i)
            end_col, i = _varint(table, i)
            yield length, line, line + extra, (col - 1 if col else None), (end_col - 1 if end_col else None)
        elif kind == 13:
            delta, i = _svarint(table, i)
            line += delta
            yield length, line, line, None, None
        elif kind >= 10:
            line += kind - 10
            col = table[i]
            end_col = table[i + 1]
            i += 2
            yield length, line, line, col, end_col
        else:
            second = table[i]
            i += 1
            col = (kind << 3) | ((second >> 4) & 7)
            yield length, line, line, col, col + (second & 15)


def _positions(table, firstlineno):
    for length, line, end_line, col, end_col in _entries(table, firstlineno):
        for _ in range(length):
            yield (line, end_line, col, end_col)


def _lines(table, firstlineno):
    start = end = 0
    line = -1
    for length, entry_line, end_line, col, end_col in _entries(table, firstlineno):
        if end and entry_line == line:
            end += 2 * length
            continue
        if end:
            yield (start, end, line)
        start = end
        end = start + 2 * length
        line = entry_line
    if end:
        yield (start, end, line)
