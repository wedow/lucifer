"""Instruction sequences shared by codegen, flowgraph and assemble.

Port of Python/instruction_sequence.c, Include/internal/pycore_instruction_sequence.h,
the _Py_SourceLocation type and the macros of Include/internal/pycore_opcode_utils.h.
"""
from collections import namedtuple

from .opcodes import *  # noqa: F403 -- opcode names are this module's vocabulary too
from . import opcodes

# Field order of _Py_SourceLocation (Include/internal/pycore_symtable.h)
SourceLocation = namedtuple("SourceLocation", "lineno end_lineno col_offset end_col_offset")
NO_LOCATION = SourceLocation(-1, -1, -1, -1)
NO_LABEL = -1
MAX_REAL_OPCODE = 254
MAX_OPCODE = 511


class ExceptHandlerInfo:
    __slots__ = ("h_label", "h_startdepth", "h_preserve_lasti")

    def __init__(self, h_label=-1, h_startdepth=-1, h_preserve_lasti=-1):
        self.h_label = h_label
        self.h_startdepth = h_startdepth
        self.h_preserve_lasti = h_preserve_lasti

    def copy(self):
        return ExceptHandlerInfo(self.h_label, self.h_startdepth, self.h_preserve_lasti)


class Instruction:
    __slots__ = ("i_opcode", "i_oparg", "i_loc", "i_except_handler_info", "i_target", "i_offset")

    def __init__(self, opcode, oparg, loc):
        self.i_opcode = opcode
        self.i_oparg = oparg
        self.i_loc = loc
        self.i_except_handler_info = ExceptHandlerInfo()
        # Temporary fields, used by the assembler and in instr_sequence_to_cfg
        self.i_target = 0
        self.i_offset = 0

    def __repr__(self):
        return f"Instruction({self.i_opcode}, {self.i_oparg}, {tuple(self.i_loc)})"


def opcode_has_arg(op):
    return bool(opcodes.flags.get(op, 0) & HAS_ARG_FLAG)


def opcode_has_const(op):
    return bool(opcodes.flags.get(op, 0) & HAS_CONST_FLAG)


def opcode_has_name(op):
    return bool(opcodes.flags.get(op, 0) & HAS_NAME_FLAG)


def opcode_has_jump(op):
    return bool(opcodes.flags.get(op, 0) & HAS_JUMP_FLAG)


def opcode_has_free(op):
    return bool(opcodes.flags.get(op, 0) & HAS_FREE_FLAG)


def opcode_has_local(op):
    return bool(opcodes.flags.get(op, 0) & HAS_LOCAL_FLAG)


def is_within_opcode_range(op):
    return 0 <= op <= MAX_REAL_OPCODE or is_pseudo_instr(op)


def is_block_push_opcode(op):
    return op in (SETUP_FINALLY, SETUP_WITH, SETUP_CLEANUP)


def has_target(op):
    return opcode_has_jump(op) or is_block_push_opcode(op)


def is_terminator_opcode(op):
    """Opcodes that must be last in the basicblock."""
    return opcode_has_jump(op) or is_scope_exit_opcode(op)


def is_assembler_opcode(op):
    """Opcodes which are not emitted in codegen stage, only by the assembler."""
    return op in (JUMP_FORWARD, JUMP_BACKWARD, JUMP_BACKWARD_NO_INTERRUPT)


def is_backwards_jump_opcode(op):
    return op in (JUMP_BACKWARD, JUMP_BACKWARD_NO_INTERRUPT)


def is_unconditional_jump_opcode(op):
    return op in (JUMP, JUMP_NO_INTERRUPT, JUMP_FORWARD, JUMP_BACKWARD, JUMP_BACKWARD_NO_INTERRUPT)


def is_conditional_jump_opcode(op):
    return op in (POP_JUMP_IF_FALSE, POP_JUMP_IF_TRUE, POP_JUMP_IF_NONE, POP_JUMP_IF_NOT_NONE)


def is_scope_exit_opcode(op):
    return op in (RETURN_VALUE, RAISE_VARARGS, RERAISE)


def is_return_opcode(op):
    return op == RETURN_VALUE


def is_raise_opcode(op):
    return op in (RAISE_VARARGS, RERAISE)


# Flags used in the oparg for MAKE_FUNCTION
MAKE_FUNCTION_DEFAULTS = 0x01
MAKE_FUNCTION_KWDEFAULTS = 0x02
MAKE_FUNCTION_ANNOTATIONS = 0x04
MAKE_FUNCTION_CLOSURE = 0x08
MAKE_FUNCTION_ANNOTATE = 0x10

# Values used as the oparg for LOAD_COMMON_CONSTANT
CONSTANT_ASSERTIONERROR = 0
CONSTANT_NOTIMPLEMENTEDERROR = 1
CONSTANT_BUILTIN_TUPLE = 2
CONSTANT_BUILTIN_ALL = 3
CONSTANT_BUILTIN_ANY = 4
NUM_COMMON_CONSTANTS = 5

# Values used in the oparg for RESUME
RESUME_AT_FUNC_START = 0
RESUME_AFTER_YIELD = 1
RESUME_AFTER_YIELD_FROM = 2
RESUME_AFTER_AWAIT = 3
RESUME_OPARG_LOCATION_MASK = 0x3
RESUME_OPARG_DEPTH1_MASK = 0x4


class InstructionSequence:
    """_PyInstructionSequence; also the Python-level API of _testinternalcapi's type."""

    def __init__(self):
        self.s_instrs = []
        self.s_next_free_label = 0
        self.s_labelmap = []      # None once applied
        self.s_nested = None
        self.s_annotations_code = None

    @property
    def s_used(self):
        return len(self.s_instrs)

    # _PyInstructionSequence_* (C API)

    def new_label(self):
        self.s_next_free_label += 1
        return self.s_next_free_label

    def use_label(self, lbl):
        if len(self.s_labelmap) <= lbl:
            self.s_labelmap.extend([-111] * (lbl + 1 - len(self.s_labelmap)))  # something weird, for debugging
        self.s_labelmap[lbl] = len(self.s_instrs)  # label refers to the next instruction

    def apply_label_map(self):
        if self.s_labelmap is None:
            return  # Already applied - nothing to do
        # Replace labels by offsets in the code
        for instr in self.s_instrs:
            if has_target(instr.i_opcode):
                instr.i_oparg = self.s_labelmap[instr.i_oparg]
            hi = instr.i_except_handler_info
            if hi.h_label >= 0:
                hi.h_label = self.s_labelmap[hi.h_label]
        # Clear label map so it's never used again
        self.s_labelmap = None

    def addop(self, opcode, oparg, loc):
        assert 0 <= opcode <= MAX_OPCODE
        assert is_within_opcode_range(opcode)
        assert opcode_has_arg(opcode) or has_target(opcode) or oparg == 0
        assert 0 <= oparg < (1 << 30)
        self.s_instrs.append(Instruction(opcode, oparg, loc))

    def insert_instruction(self, pos, opcode, oparg, loc):
        assert 0 <= pos <= len(self.s_instrs)
        self.s_instrs.insert(pos, Instruction(opcode, oparg, loc))
        # fix the labels map
        for lbl, target in enumerate(self.s_labelmap or ()):
            if target >= pos:
                self.s_labelmap[lbl] += 1

    def set_annotations_code(self, annotations):
        assert self.s_annotations_code is None
        self.s_annotations_code = annotations

    def add_nested(self, nested):
        if self.s_nested is None:
            self.s_nested = []
        self.s_nested.append(nested)

    # InstructionSequenceType methods (the Python API tests use)

    def py_addop(self, opcode, oparg, lineno, col_offset, end_lineno, end_col_offset):
        # The C initializer is positional over (lineno, end_lineno, col_offset,
        # end_col_offset), so the middle two arguments land swapped; kept as is.
        self.addop(opcode, oparg, SourceLocation(lineno, col_offset, end_lineno, end_col_offset))

    def get_nested(self):
        return [] if self.s_nested is None else self.s_nested

    def get_instructions(self):
        """The instructions as tuples (opcode, oparg, lineno, end_lineno, col_offset, end_col_offset)."""
        self.apply_label_map()
        return [(i.i_opcode, i.i_oparg if opcode_has_arg(i.i_opcode) else None,
                 i.i_loc.lineno, i.i_loc.end_lineno, i.i_loc.col_offset, i.i_loc.end_col_offset)
                for i in self.s_instrs]
