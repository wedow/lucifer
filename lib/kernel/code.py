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
