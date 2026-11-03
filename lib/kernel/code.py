"""The code type's methods (Objects/codeobject.c). The code objects, their
attributes and construction are native (runtime/interp/code.c); equality is
what the compiler's constant cache needs: two code objects with the same
bytecode, constants and tables are interchangeable."""

from _lucifer import str_join, static_type, address, code_equal, code_hash
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
