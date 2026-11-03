"""_opcode (Modules/_opcode.c): opcode properties from the compiler's
tables (Include/internal/pycore_opcode_metadata.h) and its stack effects.
Specialization and executors are absent, as in the free-threaded build."""

from compiler import flowgraph, opcodes

ENABLE_SPECIALIZATION = 0
ENABLE_SPECIALIZATION_FT = 1
_INVALID_STACK_EFFECT = flowgraph.PY_INVALID_STACK_EFFECT


def stack_effect(opcode, oparg=None, /, *, jump=None):
    """Compute the stack effect of the opcode."""
    oparg_int = 0
    if oparg is not None:
        oparg_int = oparg.__index__() if not isinstance(oparg, int) else oparg
    if jump is None:
        jump_int = -1
    elif jump is True:
        jump_int = 1
    elif jump is False:
        jump_int = 0
    else:
        raise ValueError('stack_effect: jump must be False, True or None')
    effect = flowgraph.opcode_stack_effect_with_jump(opcode, oparg_int, jump_int)
    if effect == _INVALID_STACK_EFFECT:
        raise ValueError('invalid opcode or oparg')
    return effect


def is_valid(opcode, /):
    """Return True if opcode is valid, False otherwise."""
    return opcode in opcodes.flags


def _has(flag):
    def has(opcode, /):
        return opcode in opcodes.flags and opcodes.flags[opcode] & flag != 0
    return has


has_arg = _has(opcodes.HAS_ARG_FLAG)
has_const = _has(opcodes.HAS_CONST_FLAG)
has_name = _has(opcodes.HAS_NAME_FLAG)
has_jump = _has(opcodes.HAS_JUMP_FLAG)
has_free = _has(opcodes.HAS_FREE_FLAG)
has_local = _has(opcodes.HAS_LOCAL_FLAG)


def has_exc(opcode, /):
    """Return True if the opcode sets an exception handler, False otherwise."""
    return opcode in _EXC_OPCODES


_EXC_OPCODES = frozenset(opcodes.opmap[name] for name in ('SETUP_FINALLY', 'SETUP_CLEANUP', 'SETUP_WITH'))


def get_intrinsic1_descs():
    """Return a list of names of the unary intrinsics."""
    return ['INTRINSIC_1_INVALID', 'INTRINSIC_PRINT', 'INTRINSIC_IMPORT_STAR',
            'INTRINSIC_STOPITERATION_ERROR', 'INTRINSIC_ASYNC_GEN_WRAP', 'INTRINSIC_UNARY_POSITIVE',
            'INTRINSIC_LIST_TO_TUPLE', 'INTRINSIC_TYPEVAR', 'INTRINSIC_PARAMSPEC',
            'INTRINSIC_TYPEVARTUPLE', 'INTRINSIC_SUBSCRIPT_GENERIC', 'INTRINSIC_TYPEALIAS']


def get_intrinsic2_descs():
    """Return a list of names of the binary intrinsics."""
    return ['INTRINSIC_2_INVALID', 'INTRINSIC_PREP_RERAISE_STAR', 'INTRINSIC_TYPEVAR_WITH_BOUND',
            'INTRINSIC_TYPEVAR_WITH_CONSTRAINTS', 'INTRINSIC_SET_FUNCTION_TYPE_PARAMS',
            'INTRINSIC_SET_TYPEPARAM_DEFAULT']


def get_special_method_names():
    """Return a list of special method names."""
    return ['__enter__', '__exit__', '__aenter__', '__aexit__']


def get_nb_ops():
    """Return array of symbols of binary ops.

Indexed by the BINARY_OP oparg value."""
    return [('NB_ADD', '+'), ('NB_AND', '&'), ('NB_FLOOR_DIVIDE', '//'), ('NB_LSHIFT', '<<'),
            ('NB_MATRIX_MULTIPLY', '@'), ('NB_MULTIPLY', '*'), ('NB_REMAINDER', '%'), ('NB_OR', '|'),
            ('NB_POWER', '**'), ('NB_RSHIFT', '>>'), ('NB_SUBTRACT', '-'), ('NB_TRUE_DIVIDE', '/'),
            ('NB_XOR', '^'), ('NB_INPLACE_ADD', '+='), ('NB_INPLACE_AND', '&='),
            ('NB_INPLACE_FLOOR_DIVIDE', '//='), ('NB_INPLACE_LSHIFT', '<<='),
            ('NB_INPLACE_MATRIX_MULTIPLY', '@='), ('NB_INPLACE_MULTIPLY', '*='),
            ('NB_INPLACE_REMAINDER', '%='), ('NB_INPLACE_OR', '|='), ('NB_INPLACE_POWER', '**='),
            ('NB_INPLACE_RSHIFT', '>>='), ('NB_INPLACE_SUBTRACT', '-='), ('NB_INPLACE_TRUE_DIVIDE', '/='),
            ('NB_INPLACE_XOR', '^='), ('NB_SUBSCR', '[]')]


def get_specialization_stats():
    """Return the specialization stats, or None when stats are off."""
    return None


def get_executor(code, offset):
    """Return the executor object at offset in code if exists, None otherwise."""
    if not hasattr(code, 'co_code'):
        raise TypeError("expected a code object, not '%s'" % type(code).__name__)
    raise RuntimeError('Executors are not available in this build')
