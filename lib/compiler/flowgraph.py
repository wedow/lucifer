"""The control flow graph optimizer.

Port of Python/flowgraph.c.

A block's b_instr models the C array: a list of instruction slots of which the
first b_iused are in use.  The C copies instructions by value between slots
and leaves stale values behind when a block shrinks; basicblock_addop then
reuses a slot without clearing its i_except, which shows in the exception
table (e.g. a NOT_TAKEN appended by normalize_jumps_in_block).  So slots are
objects that keep their identity, and by-value copies use assign().
"""
import operator

from .consts import const_cache_merge_one
from .instruction_sequence import (
    NO_LABEL, NO_LOCATION, SourceLocation, MAX_REAL_OPCODE, InstructionSequence,
    RESUME_AT_FUNC_START, RESUME_OPARG_DEPTH1_MASK,
    opcode_has_arg, opcode_has_const, opcode_has_jump, has_target,
    is_block_push_opcode, is_terminator_opcode, is_assembler_opcode,
    is_unconditional_jump_opcode, is_conditional_jump_opcode, is_scope_exit_opcode,
)
from .opcodes import *  # noqa: F403
from . import opcodes

INT_MIN = -(1 << 31)
INT_MAX = (1 << 31) - 1
PY_INVALID_STACK_EFFECT = INT_MAX
_PY_STACK_USE_GUIDELINE = 30

# Include/internal/pycore_intrinsics.h
INTRINSIC_UNARY_POSITIVE = 5
INTRINSIC_LIST_TO_TUPLE = 6

# Include/opcode.h
NB_ADD = 0
NB_AND = 1
NB_FLOOR_DIVIDE = 2
NB_LSHIFT = 3
NB_MATRIX_MULTIPLY = 4
NB_MULTIPLY = 5
NB_REMAINDER = 6
NB_OR = 7
NB_POWER = 8
NB_RSHIFT = 9
NB_SUBTRACT = 10
NB_TRUE_DIVIDE = 11
NB_XOR = 12
NB_SUBSCR = 26

# Include/cpython/code.h
CO_GENERATOR = 0x0020
CO_COROUTINE = 0x0080
CO_ASYNC_GENERATOR = 0x0200


def opcode_has_eval_break(op):
    return bool(opcodes.flags.get(op, 0) & HAS_EVAL_BREAK_FLAG)


class CfgInstr:
    __slots__ = ("i_opcode", "i_oparg", "i_loc", "i_target", "i_except")

    def __init__(self, opcode, oparg, loc, target=None, except_=None):
        self.i_opcode = opcode
        self.i_oparg = oparg
        self.i_loc = loc
        self.i_target = target     # target block (if jump instruction)
        self.i_except = except_    # target block when exception is raised

    def assign(self, other):
        """The C's struct assignment *self = *other."""
        self.i_opcode = other.i_opcode
        self.i_oparg = other.i_oparg
        self.i_loc = other.i_loc
        self.i_target = other.i_target
        self.i_except = other.i_except

    def set_op1(self, op, arg):
        assert opcode_has_arg(op)
        self.i_opcode = op
        self.i_oparg = arg

    def set_op0(self, op):
        assert not opcode_has_arg(op)
        self.i_opcode = op
        self.i_oparg = 0


class BasicBlock:
    __slots__ = ("b_list", "b_label", "b_exceptstack", "b_instr", "b_iused", "b_next",
                 "b_unsafe_locals_mask", "b_predecessors", "b_startdepth",
                 "b_preserve_lasti", "b_visited", "b_except_handler", "b_cold", "b_warm")

    def __init__(self):
        # Each basicblock in a compilation unit is linked via b_list in the
        # reverse order that the block are allocated.  b_list points to the next
        # block in this list, not to be confused with b_next, which is next by
        # control flow.
        self.b_list = None
        # The label of this block if it is a jump target, -1 otherwise
        self.b_label = NO_LABEL
        # Exception stack at start of block, used by assembler to create the exception handling table
        self.b_exceptstack = None
        # instruction slots; the first b_iused are in use
        self.b_instr = []
        self.b_iused = 0
        # If b_next is non-None, it is the next block reached by normal control flow.
        self.b_next = None
        # Used by add_checks_for_loads_of_unknown_variables
        self.b_unsafe_locals_mask = 0
        # Number of predecessors that a block has.
        self.b_predecessors = 0
        # depth of stack upon entry of block, computed by stackdepth()
        self.b_startdepth = 0
        # Basic block is an exception handler that preserves lasti
        self.b_preserve_lasti = 0
        # Used by compiler passes to mark whether they have visited a basic block.
        self.b_visited = 0
        # b_except_handler is used by the cold-detection algorithm to mark exception targets
        self.b_except_handler = 0
        # b_cold is true if this block is not perf critical (like an exception handler)
        self.b_cold = 0
        # b_warm is used by the cold-detection algorithm to mark blocks which are definitely not cold
        self.b_warm = 0

    def used(self):
        """The instructions in use, for loops that do not change the block."""
        return self.b_instr[:self.b_iused]


class CfgBuilder:
    def __init__(self):
        # The entryblock, at which control flow begins. All blocks of the
        # CFG are reachable through the b_next links
        self.g_entryblock = None
        # Pointer to the most recently allocated block.  By following
        # b_list links, you can reach all allocated blocks.
        self.g_block_list = None
        # pointer to the block currently being constructed
        self.g_curblock = None
        # label for the next instruction to be placed
        self.g_current_label = NO_LABEL
        block = cfg_builder_new_block(self)
        self.g_curblock = self.g_entryblock = block


def is_label(lbl):
    return lbl != NO_LABEL


def is_block_push(i):
    return is_block_push_opcode(i.i_opcode)


def is_jump(i):
    return opcode_has_jump(i.i_opcode)


# ***** Blocks *****

_ZERO_LOCATION = SourceLocation(0, 0, 0, 0)


def basicblock_next_instr(b):
    """The index of the next slot, which is zeroed memory if never used."""
    if b.b_iused == len(b.b_instr):
        b.b_instr.append(CfgInstr(0, 0, _ZERO_LOCATION))
    b.b_iused += 1
    return b.b_iused - 1


def basicblock_last_instr(b):
    return b.b_instr[b.b_iused - 1] if b.b_iused > 0 else None


def cfg_builder_new_block(g):
    b = BasicBlock()
    # Extend the singly linked list of blocks with new block.
    b.b_list = g.g_block_list
    g.g_block_list = b
    return b


def basicblock_addop(b, opcode, oparg, loc):
    assert not is_assembler_opcode(opcode)
    assert opcode_has_arg(opcode) or has_target(opcode) or oparg == 0
    assert 0 <= oparg < (1 << 30)
    i = b.b_instr[basicblock_next_instr(b)]
    i.i_opcode = opcode
    i.i_oparg = oparg
    i.i_target = None
    i.i_loc = loc


def basicblock_add_jump(b, opcode, target, loc):
    last = basicblock_last_instr(b)
    if last and is_jump(last):
        raise SystemError("jump after jump")
    basicblock_addop(b, opcode, target.b_label, loc)
    basicblock_last_instr(b).i_target = target


def basicblock_append_instructions(to, from_):
    for i in range(from_.b_iused):
        n = basicblock_next_instr(to)
        to.b_instr[n].assign(from_.b_instr[i])


def basicblock_nofallthrough(b):
    last = basicblock_last_instr(b)
    return bool(last and (is_scope_exit_opcode(last.i_opcode) or
                          is_unconditional_jump_opcode(last.i_opcode)))


def bb_no_fallthrough(b):
    return basicblock_nofallthrough(b)


def bb_has_fallthrough(b):
    return not basicblock_nofallthrough(b)


def copy_basicblock(g, block):
    # Cannot copy a block if it has a fallthrough, since
    # a block can only have one fallthrough predecessor.
    assert bb_no_fallthrough(block)
    result = cfg_builder_new_block(g)
    basicblock_append_instructions(result, block)
    return result


def basicblock_insert_instruction(block, pos, instr):
    basicblock_next_instr(block)
    slots = block.b_instr
    for i in range(block.b_iused - 1, pos, -1):
        slots[i].assign(slots[i - 1])
    slots[pos].assign(instr)


# ***** CFG construction and modification *****

def cfg_builder_use_next_block(g, block):
    assert block is not None
    g.g_curblock.b_next = block
    g.g_curblock = block
    return block


def basicblock_exits_scope(b):
    last = basicblock_last_instr(b)
    return bool(last and is_scope_exit_opcode(last.i_opcode))


def basicblock_has_eval_break(b):
    return any(opcode_has_eval_break(instr.i_opcode) for instr in b.used())


def cfg_builder_current_block_is_terminated(g):
    last = basicblock_last_instr(g.g_curblock)
    if last and is_terminator_opcode(last.i_opcode):
        return True
    if is_label(g.g_current_label):
        if last or is_label(g.g_curblock.b_label):
            return True
        # current block is empty, label it
        g.g_curblock.b_label = g.g_current_label
        g.g_current_label = NO_LABEL
    return False


def cfg_builder_maybe_start_new_block(g):
    if cfg_builder_current_block_is_terminated(g):
        b = cfg_builder_new_block(g)
        b.b_label = g.g_current_label
        g.g_current_label = NO_LABEL
        cfg_builder_use_next_block(g, b)


def cfg_builder_use_label(g, lbl):
    """_PyCfgBuilder_UseLabel"""
    g.g_current_label = lbl
    cfg_builder_maybe_start_new_block(g)


def cfg_builder_addop(g, opcode, oparg, loc):
    """_PyCfgBuilder_Addop"""
    cfg_builder_maybe_start_new_block(g)
    basicblock_addop(g.g_curblock, opcode, oparg, loc)


def cfg_builder_free(g):
    """_PyCfgBuilder_Free: release every block and its instructions.

    Blocks and instructions refer to each other (jump targets, handlers), so
    the links are cut here as the C frees them, rather than leaving cycles
    for the garbage collector; until then they would keep objects such as
    the ints of source locations alive and change marshal's reference flags.
    """
    if g is None:
        return
    b = g.g_block_list
    while b is not None:
        b.b_instr = []
        b.b_iused = 0
        b.b_next = b.b_exceptstack = None
        b.b_list, b = None, b.b_list
    g.g_entryblock = g.g_block_list = g.g_curblock = None


def next_nonempty_block(b):
    while b is not None and b.b_iused == 0:
        b = b.b_next
    return b


# ***** CFG preprocessing (jump targets and exceptions) *****

def normalize_jumps_in_block(g, b):
    last = basicblock_last_instr(b)
    if last is None or not is_conditional_jump_opcode(last.i_opcode):
        return
    assert not is_assembler_opcode(last.i_opcode)

    is_forward = last.i_target.b_visited == 0
    if is_forward:
        basicblock_addop(b, NOT_TAKEN, 0, last.i_loc)
        return

    reversed_opcode = {
        POP_JUMP_IF_NOT_NONE: POP_JUMP_IF_NONE,
        POP_JUMP_IF_NONE: POP_JUMP_IF_NOT_NONE,
        POP_JUMP_IF_FALSE: POP_JUMP_IF_TRUE,
        POP_JUMP_IF_TRUE: POP_JUMP_IF_FALSE,
    }[last.i_opcode]
    # transform 'conditional jump T' to
    # 'reversed_jump b_next' followed by 'jump_backwards T'
    target = last.i_target
    backwards_jump = cfg_builder_new_block(g)
    basicblock_addop(backwards_jump, NOT_TAKEN, 0, last.i_loc)
    basicblock_add_jump(backwards_jump, JUMP, target, last.i_loc)
    backwards_jump.b_startdepth = target.b_startdepth
    last.i_opcode = reversed_opcode
    last.i_target = b.b_next

    backwards_jump.b_cold = b.b_cold
    backwards_jump.b_next = b.b_next
    b.b_next = backwards_jump


def normalize_jumps(g):
    b = g.g_entryblock
    while b is not None:
        b.b_visited = 0
        b = b.b_next
    b = g.g_entryblock
    while b is not None:
        b.b_visited = 1
        normalize_jumps_in_block(g, b)
        b = b.b_next


def blocks(entryblock):
    """The blocks in b_next order; the chain is read lazily, as the C loops do."""
    b = entryblock
    while b is not None:
        yield b
        b = b.b_next


def check_cfg(g):
    for b in blocks(g.g_entryblock):
        # Raise SystemError if jump or exit is not last instruction in the block.
        for i, instr in enumerate(b.used()):
            assert not is_assembler_opcode(instr.i_opcode)
            if is_terminator_opcode(instr.i_opcode) and i != b.b_iused - 1:
                raise SystemError("malformed control flow graph.")


def get_max_label(entryblock):
    lbl = -1
    for b in blocks(entryblock):
        if b.b_label > lbl:
            lbl = b.b_label
    return lbl


def translate_jump_labels_to_targets(entryblock):
    """Calculate the actual jump target from the target_label"""
    max_label = get_max_label(entryblock)
    label2block = [None] * (max_label + 1)
    for b in blocks(entryblock):
        if b.b_label >= 0:
            label2block[b.b_label] = b
    for b in blocks(entryblock):
        for instr in b.used():
            assert instr.i_target is None
            if has_target(instr.i_opcode):
                lbl = instr.i_oparg
                assert 0 <= lbl <= max_label
                instr.i_target = label2block[lbl]
                assert instr.i_target is not None
                assert instr.i_target.b_label == lbl


def mark_except_handlers(entryblock):
    for b in blocks(entryblock):
        for instr in b.used():
            if is_block_push(instr):
                instr.i_target.b_except_handler = 1


# The except stack is a list of handler blocks whose item 0 is None; its depth
# is len(stack) - 1.

def push_except_block(stack, setup):
    assert is_block_push(setup)
    opcode = setup.i_opcode
    target = setup.i_target
    if opcode == SETUP_WITH or opcode == SETUP_CLEANUP:
        target.b_preserve_lasti = 1
    stack.append(target)
    return target


def pop_except_block(stack):
    assert len(stack) > 1
    stack.pop()
    return stack[-1]


def except_stack_top(stack):
    return stack[-1]


def make_cfg_traversal_stack(entryblock):
    for b in blocks(entryblock):
        b.b_visited = 0
    return []


def get_stack_effects(opcode, oparg, jump):
    """Compute the stack effects of opcode with argument oparg, or None.

    Some opcodes have different stack effect when jump to the target and
    when not jump. The 'jump' parameter specifies the case:

    * 0 -- when not jump
    * 1 -- when jump
    * -1 -- maximal
    """
    if opcode < 0:
        return None
    if opcode <= MAX_REAL_OPCODE and opcodes.deopt.get(opcode, 0) != opcode:
        # Specialized instructions are not supported.
        return None
    popped = opcodes.num_popped(opcode, oparg)
    pushed = opcodes.num_pushed(opcode, oparg)
    if popped < 0 or pushed < 0:
        return None
    if is_block_push_opcode(opcode) and not jump:
        return 0
    return pushed - popped


def stackdepth_push(stack, b, depth):
    if not (b.b_startdepth < 0 or b.b_startdepth == depth):
        raise ValueError("Invalid CFG, inconsistent stackdepth")
    if b.b_startdepth < depth and b.b_startdepth < 100:
        assert b.b_startdepth < 0
        b.b_startdepth = depth
        stack.append(b)


def calculate_stackdepth(g):
    """Find the flow path that needs the largest stack.  We assume that
    cycles in the flow graph have no net effect on the stack depth.
    """
    entryblock = g.g_entryblock
    for b in blocks(entryblock):
        b.b_startdepth = INT_MIN
    stack = make_cfg_traversal_stack(entryblock)

    maxdepth = 0
    stackdepth_push(stack, entryblock, 0)
    while stack:
        b = stack.pop()
        depth = b.b_startdepth
        assert depth >= 0
        next_ = b.b_next
        for instr in b.used():
            effect = get_stack_effects(instr.i_opcode, instr.i_oparg, 0)
            if effect is None:
                raise SystemError(f"Invalid stack effect for opcode={instr.i_opcode}, arg={instr.i_oparg}")
            new_depth = depth + effect
            if new_depth < 0:
                raise ValueError("Invalid CFG, stack underflow")
            maxdepth = max(maxdepth, depth)
            if has_target(instr.i_opcode) and instr.i_opcode != END_ASYNC_FOR:
                effect = get_stack_effects(instr.i_opcode, instr.i_oparg, 1)
                if effect is None:
                    raise SystemError(f"Invalid stack effect for opcode={instr.i_opcode}, arg={instr.i_oparg}")
                target_depth = depth + effect
                assert target_depth >= 0  # invalid code or bug in stackdepth()
                maxdepth = max(maxdepth, depth)
                stackdepth_push(stack, instr.i_target, target_depth)
            depth = new_depth
            assert not is_assembler_opcode(instr.i_opcode)
            if is_unconditional_jump_opcode(instr.i_opcode) or is_scope_exit_opcode(instr.i_opcode):
                # remaining code is dead
                next_ = None
                break
        if next_ is not None:
            assert bb_has_fallthrough(b)
            stackdepth_push(stack, next_, depth)
    return maxdepth


def label_exception_targets(entryblock):
    todo = make_cfg_traversal_stack(entryblock)
    except_stack = [None]
    todo.append(entryblock)
    entryblock.b_visited = 1
    entryblock.b_exceptstack = except_stack
    while todo:
        b = todo.pop()
        assert b.b_visited == 1
        except_stack = b.b_exceptstack
        assert except_stack is not None
        b.b_exceptstack = None
        handler = except_stack_top(except_stack)
        last_yield_except_depth = -1
        for i, instr in enumerate(b.used()):
            if is_block_push(instr):
                if not instr.i_target.b_visited:
                    instr.i_target.b_exceptstack = list(except_stack)
                    todo.append(instr.i_target)
                    instr.i_target.b_visited = 1
                handler = push_except_block(except_stack, instr)
            elif instr.i_opcode == POP_BLOCK:
                handler = pop_except_block(except_stack)
                instr.set_op0(NOP)
            elif is_jump(instr):
                instr.i_except = handler
                assert i == b.b_iused - 1
                if not instr.i_target.b_visited:
                    if bb_has_fallthrough(b):
                        instr.i_target.b_exceptstack = list(except_stack)
                    else:
                        instr.i_target.b_exceptstack = except_stack
                        except_stack = None
                    todo.append(instr.i_target)
                    instr.i_target.b_visited = 1
            elif instr.i_opcode == YIELD_VALUE:
                instr.i_except = handler
                last_yield_except_depth = len(except_stack) - 1
            elif instr.i_opcode == RESUME:
                instr.i_except = handler
                if instr.i_oparg != RESUME_AT_FUNC_START:
                    assert last_yield_except_depth >= 0
                    if last_yield_except_depth == 1:
                        instr.i_oparg |= RESUME_OPARG_DEPTH1_MASK
                    last_yield_except_depth = -1
            else:
                instr.i_except = handler
        if bb_has_fallthrough(b) and not b.b_next.b_visited:
            assert except_stack is not None
            b.b_next.b_exceptstack = except_stack
            todo.append(b.b_next)
            b.b_next.b_visited = 1


# ***** CFG optimizations *****

def remove_unreachable(entryblock):
    for b in blocks(entryblock):
        b.b_predecessors = 0
    stack = make_cfg_traversal_stack(entryblock)
    entryblock.b_predecessors = 1
    stack.append(entryblock)
    entryblock.b_visited = 1
    while stack:
        b = stack.pop()
        if b.b_next and bb_has_fallthrough(b):
            if not b.b_next.b_visited:
                assert b.b_next.b_predecessors == 0
                stack.append(b.b_next)
                b.b_next.b_visited = 1
            b.b_next.b_predecessors += 1
        for instr in b.used():
            if is_jump(instr) or is_block_push(instr):
                target = instr.i_target
                if not target.b_visited:
                    stack.append(target)
                    target.b_visited = 1
                target.b_predecessors += 1

    # Delete unreachable instructions
    for b in blocks(entryblock):
        if b.b_predecessors == 0:
            b.b_iused = 0
            b.b_except_handler = 0


def basicblock_remove_redundant_nops(bb):
    """Remove NOPs when legal to do so."""
    instrs = bb.b_instr
    dest = 0
    prev_lineno = -1
    n = bb.b_iused
    for src in range(n):
        instr = instrs[src]
        lineno = instr.i_loc.lineno
        if instr.i_opcode == NOP:
            # Eliminate no-op if it doesn't have a line number
            if lineno < 0:
                continue
            # or, if the previous instruction had the same line number.
            if prev_lineno == lineno:
                continue
            # or, if the next instruction has same line number or no line number
            if src < n - 1:
                next_lineno = instrs[src + 1].i_loc.lineno
                if next_lineno == lineno:
                    continue
                if next_lineno < 0:
                    instrs[src + 1].i_loc = instr.i_loc
                    continue
            else:
                next_ = next_nonempty_block(bb.b_next)
                # or if last instruction in BB and next BB has same line number
                if next_:
                    next_loc = NO_LOCATION
                    for ninstr in next_.used():
                        if ninstr.i_opcode == NOP and ninstr.i_loc.lineno < 0:
                            # Skip over NOPs without a location, they will be removed
                            continue
                        next_loc = ninstr.i_loc
                        break
                    if lineno == next_loc.lineno:
                        continue
        if dest != src:
            instrs[dest].assign(instr)
        dest += 1
        prev_lineno = lineno
    num_removed = n - dest
    bb.b_iused = dest
    return num_removed


def remove_redundant_nops(g):
    changes = 0
    for b in blocks(g.g_entryblock):
        changes += basicblock_remove_redundant_nops(b)
    return changes


def remove_redundant_nops_and_pairs(entryblock):
    done = False
    while not done:
        done = True
        instr = None
        for b in blocks(entryblock):
            basicblock_remove_redundant_nops(b)
            if is_label(b.b_label):
                # this block is a jump target, forget instr
                instr = None
            for cur in b.used():
                prev_instr = instr
                instr = cur
                prev_opcode = prev_instr.i_opcode if prev_instr else 0
                prev_oparg = prev_instr.i_oparg if prev_instr else 0
                opcode = instr.i_opcode
                is_redundant_pair = False
                if opcode == POP_TOP:
                    if prev_opcode == LOAD_CONST or prev_opcode == LOAD_SMALL_INT:
                        is_redundant_pair = True
                    elif prev_opcode == COPY and prev_oparg == 1:
                        is_redundant_pair = True
                if is_redundant_pair:
                    prev_instr.set_op0(NOP)
                    instr.set_op0(NOP)
                    done = False
            if (instr and is_jump(instr)) or not bb_has_fallthrough(b):
                instr = None


def remove_redundant_jumps(g):
    """If a non-empty block ends with a jump instruction, check if the next
    non-empty block reached through normal flow control is the target
    of that jump. If it is, then the jump instruction is redundant and
    can be deleted.

    Return the number of changes applied.
    """
    changes = 0
    for b in blocks(g.g_entryblock):
        last = basicblock_last_instr(b)
        if last is None:
            continue
        assert not is_assembler_opcode(last.i_opcode)
        if is_unconditional_jump_opcode(last.i_opcode):
            jump_target = next_nonempty_block(last.i_target)
            if jump_target is None:
                raise SystemError("jump with NULL target")
            next_ = next_nonempty_block(b.b_next)
            if jump_target is next_:
                changes += 1
                last.set_op0(NOP)
    return changes


def basicblock_has_no_lineno(b):
    return all(instr.i_loc.lineno < 0 for instr in b.used())


# Maximum size of basic block that should be copied in optimizer
MAX_COPY_SIZE = 4


def basicblock_inline_small_or_no_lineno_blocks(bb):
    """If this block ends with an unconditional jump to a small exit block or
    a block that has no line numbers (and no fallthrough), then
    remove the jump and extend this block with the target.
    Returns True if extended.
    """
    last = basicblock_last_instr(bb)
    if last is None:
        return False
    if not is_unconditional_jump_opcode(last.i_opcode):
        return False
    target = last.i_target
    small_exit_block = basicblock_exits_scope(target) and target.b_iused <= MAX_COPY_SIZE
    no_lineno_no_fallthrough = basicblock_has_no_lineno(target) and not bb_has_fallthrough(target)
    if small_exit_block or no_lineno_no_fallthrough:
        assert is_jump(last)
        removed_jump_opcode = last.i_opcode
        last.set_op0(NOP)
        basicblock_append_instructions(bb, target)
        if no_lineno_no_fallthrough:
            last = basicblock_last_instr(bb)
            if is_unconditional_jump_opcode(last.i_opcode) and removed_jump_opcode == JUMP:
                # Make sure we don't lose eval breaker checks
                last.i_opcode = JUMP
        target.b_predecessors -= 1
        return True
    return False


def inline_small_or_no_lineno_blocks(entryblock):
    while True:
        changes = False
        for b in blocks(entryblock):
            if basicblock_inline_small_or_no_lineno_blocks(b):
                changes = True
        if not changes:  # every change removes a jump, ensuring convergence
            return


def jump_thread(bb, inst, target, opcode):
    """Attempt to eliminate jumps to jumps by updating inst to jump to
    target->i_target using the provided opcode. Return whether or not the
    optimization was successful.
    """
    assert is_jump(inst)
    assert is_jump(target)
    assert inst is basicblock_last_instr(bb)
    # bpo-45773: If inst->i_target == target->i_target, then nothing actually
    # changes (and we fall into an infinite loop):
    if inst.i_target is not target.i_target:
        # Change inst to NOP and append a jump to target->i_target. The
        # NOP will be removed later if it's not needed for the lineno.
        inst.set_op0(NOP)
        basicblock_add_jump(bb, opcode, target.i_target, target.i_loc)
        return True
    return False


def loads_const(opcode):
    return opcode_has_const(opcode) or opcode == LOAD_SMALL_INT


def get_const_value(opcode, oparg, co_consts):
    assert loads_const(opcode)
    constant = None
    if opcode == LOAD_CONST:
        n = len(co_consts)
        if oparg < 0 or oparg >= n:
            raise ValueError(f"LOAD_CONST index {oparg} is out of range for consts (len={n})")
        constant = co_consts[oparg]
    if opcode == LOAD_SMALL_INT:
        return oparg
    if opcode != LOAD_CONST:
        raise SystemError("Internal error: failed to get value of a constant")
    return constant


def add_const(newconst, consts, const_cache, consts_index):
    """The index of newconst in consts, appending it if needed.

    consts_index maps id() of each object in consts to its index, as the C's
    pointer hashtable does; consts keeps those objects alive.
    """
    newconst = const_cache_merge_one(const_cache, newconst)
    index = consts_index.get(id(newconst))
    if index is not None:
        return index
    index = len(consts)
    if index >= INT_MAX - 1:
        raise OverflowError("too many constants")
    consts.append(newconst)
    consts_index[id(newconst)] = index
    return index


def get_const_loading_instrs(bb, start, size):
    """Traverse the instructions of the basic block backwards from index
    "start", skipping over NOPs.  Collect "size" consecutive instructions
    that load constants, in program order, or None if there are not that
    many.
    """
    assert start < bb.b_iused
    assert 0 <= size <= _PY_STACK_USE_GUIDELINE
    instrs = [None] * size
    while start >= 0 and size > 0:
        instr = bb.b_instr[start]
        start -= 1
        if instr.i_opcode == NOP:
            continue
        if not loads_const(instr.i_opcode):
            return None
        size -= 1
        instrs[size] = instr
    return instrs if size == 0 else None


def nop_out(instrs):
    """Change every instruction in instrs to NOP and set its location to NO_LOCATION."""
    for instr in instrs:
        assert instr.i_opcode != NOP
        instr.set_op0(NOP)
        instr.i_loc = NO_LOCATION


def maybe_instr_make_load_smallint(instr, newconst, consts, const_cache):
    """Change instr to LOAD_SMALL_INT if newconst is an int in 0..255."""
    if type(newconst) is int:
        if 0 <= newconst <= 255:
            instr.set_op1(LOAD_SMALL_INT, newconst)
            return True
    return False


def instr_make_load_const(instr, newconst, consts, const_cache, consts_index):
    if maybe_instr_make_load_smallint(instr, newconst, consts, const_cache):
        return
    oparg = add_const(newconst, consts, const_cache, consts_index)
    instr.set_op1(LOAD_CONST, oparg)


def fold_tuple_of_constants(bb, i, consts, const_cache, consts_index):
    """Replace LOAD_CONST c1, LOAD_CONST c2 ... LOAD_CONST cn, BUILD_TUPLE n
    with    LOAD_CONST (c1, c2, ... cn).
    The consts table must still be in list form so that the
    new constant (c1, c2, ... cn) can be appended.
    """
    instr = bb.b_instr[i]
    assert instr.i_opcode == BUILD_TUPLE

    seq_size = instr.i_oparg
    if seq_size > _PY_STACK_USE_GUIDELINE:
        return
    const_instrs = get_const_loading_instrs(bb, i - 1, seq_size)
    if const_instrs is None:
        # not a const sequence
        return
    const_tuple = tuple(get_const_value(inst.i_opcode, inst.i_oparg, consts) for inst in const_instrs)
    nop_out(const_instrs)
    instr_make_load_const(instr, const_tuple, consts, const_cache, consts_index)


def fold_constant_seq_into_load_const(bb, i, consts, const_cache, consts_index):
    """Replace:
        BUILD_LIST/BUILD_SET 0
        LOAD_CONST c1
        LIST_APPEND/SET_ADD 1
        LOAD_CONST c2
        LIST_APPEND/SET_ADD 1
        ...
        LOAD_CONST cN
        LIST_APPEND/SET_ADD 1
        [CALL_INTRINSIC_1 INTRINSIC_LIST_TO_TUPLE]   <-- optional
       with:
        LOAD_CONST (c1, c2, ... cN)
       The instruction at `i` is either the LIST_TO_TUPLE intrinsic (so the
       immediately preceding non-NOP instruction is expected to be a
       LIST_APPEND, and only the BUILD_LIST/LIST_APPEND form is considered),
       or the trailing LIST_APPEND or SET_ADD itself, in which case the
       matching BUILD_LIST/BUILD_SET start is selected from its opcode, and
       for sets the result is wrapped in a frozenset.
    """
    assert 0 <= i < bb.b_iused
    target = bb.b_instr[i]
    assert (target.i_opcode in (LIST_APPEND, SET_ADD) or
            (target.i_opcode == CALL_INTRINSIC_1 and target.i_oparg == INTRINSIC_LIST_TO_TUPLE))
    expected_append = target.i_opcode == CALL_INTRINSIC_1
    append_op = LIST_APPEND if expected_append else target.i_opcode
    build_op = BUILD_LIST if append_op == LIST_APPEND else BUILD_SET
    consts_found = 0
    # Walking backward from `i`, we expect LIST_APPEND/SET_ADD and
    # LOAD_CONST to alternate. If `i` is the trailing LIST_TO_TUPLE
    # intrinsic, the next instruction back is an APPEND. If `i` is the
    # trailing APPEND itself, the next instruction back is a LOAD_CONST.
    expect_append = expected_append

    for pos in range(i - 1, -1, -1):
        instr = bb.b_instr[pos]
        opcode = instr.i_opcode
        oparg = instr.i_oparg

        if opcode == NOP:
            continue

        if opcode == build_op and oparg == 0:
            if not expect_append:
                # Not a sequence start.
                return

            # Sequence start, we are done.
            items = [None] * consts_found
            newpos_start = i - 1 if expected_append else i
            for newpos in range(newpos_start, pos - 1, -1):
                instr = bb.b_instr[newpos]
                if instr.i_opcode == NOP:
                    continue
                if loads_const(instr.i_opcode):
                    assert consts_found > 0
                    consts_found -= 1
                    items[consts_found] = get_const_value(instr.i_opcode, instr.i_oparg, consts)
                nop_out((instr,))
            assert consts_found == 0
            newconst = tuple(items)
            if build_op == BUILD_SET:
                newconst = frozenset(newconst)
            instr_make_load_const(target, newconst, consts, const_cache, consts_index)
            return

        if expect_append:
            if opcode != append_op or oparg != 1:
                return
        else:
            if not loads_const(opcode):
                return
            consts_found += 1

        expect_append = not expect_append

    # Did not find sequence start.


MIN_CONST_SEQUENCE_SIZE = 3


def optimize_lists_and_sets(bb, i, nextop, consts, const_cache, consts_index):
    """Optimize lists and sets for:
        1. "for" loop, comprehension or "in"/"not in" tests:
               Change literal list or set of constants into constant
               tuple or frozenset respectively. Change list of
               non-constants into tuple.
        2. Constant literal lists/set with length >= MIN_CONST_SEQUENCE_SIZE:
               Replace LOAD_CONST c1, LOAD_CONST c2 ... LOAD_CONST cN, BUILD_LIST N
               with BUILD_LIST 0, LOAD_CONST (c1, c2, ... cN), LIST_EXTEND 1,
               or BUILD_SET & SET_UPDATE respectively.
    """
    instr = bb.b_instr[i]
    assert instr.i_opcode in (BUILD_LIST, BUILD_SET)

    contains_or_iter = nextop == GET_ITER or nextop == CONTAINS_OP
    seq_size = instr.i_oparg
    if seq_size > _PY_STACK_USE_GUIDELINE or (seq_size < MIN_CONST_SEQUENCE_SIZE and not contains_or_iter):
        return

    const_instrs = get_const_loading_instrs(bb, i - 1, seq_size)
    if const_instrs is None:  # not a const sequence
        if contains_or_iter and instr.i_opcode == BUILD_LIST:
            # iterate over a tuple instead of list
            instr.set_op1(BUILD_TUPLE, instr.i_oparg)
        return

    const_result = tuple(get_const_value(inst.i_opcode, inst.i_oparg, consts) for inst in const_instrs)
    if instr.i_opcode == BUILD_SET:
        const_result = frozenset(const_result)

    index = add_const(const_result, consts, const_cache, consts_index)
    nop_out(const_instrs)

    if contains_or_iter:
        instr.set_op1(LOAD_CONST, index)
    else:
        assert i >= 2
        b = bb.b_instr
        b[i - 2].i_loc = instr.i_loc
        b[i - 2].set_op1(instr.i_opcode, 0)
        b[i - 1].set_op1(LOAD_CONST, index)
        b[i].set_op1(LIST_EXTEND if instr.i_opcode == BUILD_LIST else SET_UPDATE, 1)


def const_folding_check_complexity(obj, limit):
    """Check whether the total number of items in the (possibly nested)
    collection obj exceeds limit. Return a negative number if it does, and a
    non-negative number otherwise.  Used to avoid creating constants which
    are slow to hash.
    """
    if isinstance(obj, tuple):
        limit -= len(obj)
        i = 0
        while limit >= 0 and i < len(obj):
            limit = const_folding_check_complexity(obj[i], limit)
            if limit < 0:
                return limit
            i += 1
    return limit


MAX_INT_SIZE = 128          # bits
MAX_COLLECTION_SIZE = 256   # items
MAX_STR_SIZE = 4096         # characters
MAX_TOTAL_ITEMS = 1024      # including nested collections

_LONG_MIN = -(1 << 63)
_LONG_MAX = (1 << 63) - 1
_SIZE_MAX = (1 << 64) - 1


class _NoFold(Exception):
    """A C helper returned NULL: the operation is not folded."""


def _as_long(v):
    """PyLong_AsLong; an overflow means no folding."""
    if not _LONG_MIN <= v <= _LONG_MAX:
        raise _NoFold
    return int(v)


def _as_size_t(v):
    """PyLong_AsSize_t; a negative or overflowing value means no folding."""
    if not 0 <= v <= _SIZE_MAX:
        raise _NoFold
    return int(v)


def const_folding_safe_multiply(v, w):
    if isinstance(v, int) and isinstance(w, int) and v != 0 and w != 0:
        vbits = abs(v).bit_length()
        wbits = abs(w).bit_length()
        if vbits + wbits > MAX_INT_SIZE:
            raise _NoFold
    elif isinstance(v, int) and isinstance(w, tuple):
        size = len(w)
        if size:
            n = _as_long(v)
            if n < 0 or n > MAX_COLLECTION_SIZE // size:
                raise _NoFold
            if n and const_folding_check_complexity(w, MAX_TOTAL_ITEMS // n) < 0:
                raise _NoFold
    elif isinstance(v, int) and isinstance(w, (str, bytes)):
        size = len(w)
        if size:
            n = _as_long(v)
            if n < 0 or n > MAX_STR_SIZE // size:
                raise _NoFold
    elif isinstance(w, int) and isinstance(v, (tuple, str, bytes)):
        return const_folding_safe_multiply(w, v)
    return operator.mul(v, w)


def const_folding_safe_power(v, w):
    if isinstance(v, int) and isinstance(w, int) and v != 0 and w > 0:
        vbits = abs(v).bit_length()
        wbits = _as_size_t(w)
        if vbits > MAX_INT_SIZE // wbits:
            raise _NoFold
    return pow(v, w)


def const_folding_safe_lshift(v, w):
    if isinstance(v, int) and isinstance(w, int) and v != 0 and w != 0:
        vbits = abs(v).bit_length()
        wbits = _as_size_t(w)
        if wbits > MAX_INT_SIZE or vbits > MAX_INT_SIZE - wbits:
            raise _NoFold
    return operator.lshift(v, w)


def const_folding_safe_mod(v, w):
    if isinstance(v, (str, bytes)):
        raise _NoFold
    return operator.mod(v, w)


def _matrix_multiply(v, w):
    # No builtin constants implement matrix multiplication
    raise _NoFold


_BINOPS = {
    NB_ADD: operator.add,
    NB_SUBTRACT: operator.sub,
    NB_MULTIPLY: const_folding_safe_multiply,
    NB_TRUE_DIVIDE: operator.truediv,
    NB_FLOOR_DIVIDE: operator.floordiv,
    NB_REMAINDER: const_folding_safe_mod,
    NB_POWER: const_folding_safe_power,
    NB_LSHIFT: const_folding_safe_lshift,
    NB_RSHIFT: operator.rshift,
    NB_OR: operator.or_,
    NB_XOR: operator.xor,
    NB_AND: operator.and_,
    NB_SUBSCR: operator.getitem,
    NB_MATRIX_MULTIPLY: _matrix_multiply,
}


def eval_const_binop(left, op, right):
    """The folded value, or raise _NoFold (the C's NULL)."""
    try:
        return _BINOPS[op](left, right)
    except (_NoFold, KeyboardInterrupt):
        raise
    except Exception:
        raise _NoFold


def fold_const_binop(bb, i, consts, const_cache, consts_index):
    binop = bb.b_instr[i]
    assert binop.i_opcode == BINARY_OP

    operands_instrs = get_const_loading_instrs(bb, i - 1, 2)
    if operands_instrs is None:
        # not a const sequence
        return
    lhs_instr, rhs_instr = operands_instrs
    lhs = get_const_value(lhs_instr.i_opcode, lhs_instr.i_oparg, consts)
    rhs = get_const_value(rhs_instr.i_opcode, rhs_instr.i_oparg, consts)
    try:
        newconst = eval_const_binop(lhs, binop.i_oparg, rhs)
    except _NoFold:
        return
    nop_out(operands_instrs)
    instr_make_load_const(binop, newconst, consts, const_cache, consts_index)


def eval_const_unaryop(operand, opcode, oparg):
    try:
        if opcode == UNARY_NEGATIVE:
            return operator.neg(operand)
        if opcode == UNARY_INVERT:
            # XXX: This should be removed once the ~bool depreciation expires.
            if isinstance(operand, bool):
                raise _NoFold
            return operator.invert(operand)
        if opcode == UNARY_NOT:
            return not operand
        assert opcode == CALL_INTRINSIC_1 and oparg == INTRINSIC_UNARY_POSITIVE
        return operator.pos(operand)
    except (_NoFold, KeyboardInterrupt):
        raise
    except Exception:
        raise _NoFold


def fold_const_unaryop(bb, i, consts, const_cache, consts_index):
    unaryop = bb.b_instr[i]
    found = get_const_loading_instrs(bb, i - 1, 1)
    if found is None:
        # not a const
        return
    operand_instr, = found
    operand = get_const_value(operand_instr.i_opcode, operand_instr.i_oparg, consts)
    try:
        newconst = eval_const_unaryop(operand, unaryop.i_opcode, unaryop.i_oparg)
    except _NoFold:
        return
    if unaryop.i_opcode == UNARY_NOT:
        assert isinstance(newconst, bool)
    nop_out(found)
    instr_make_load_const(unaryop, newconst, consts, const_cache, consts_index)


VISITED = -1


def swaptimize(block, ix):
    """Replace an arbitrary run of SWAPs and NOPs with an optimal one that has
    the same effect.  Returns the index of the run's last instruction."""
    # NOTE: "./python -m test test_patma" serves as a good, quick stress test
    # for this function. Make sure to blow away cached *.pyc files first!
    assert ix < block.b_iused
    instrs = block.b_instr
    # Find the length of the current sequence of SWAPs and NOPs, and record the
    # maximum depth of the stack manipulations:
    assert instrs[ix].i_opcode == SWAP
    depth = instrs[ix].i_oparg
    length = 0
    more = False
    limit = block.b_iused - ix
    while True:
        length += 1
        if length >= limit:
            break
        opcode = instrs[ix + length].i_opcode
        if opcode == SWAP:
            depth = max(depth, instrs[ix + length].i_oparg)
            more = True
        elif opcode != NOP:
            break
    # It's already optimal if there's only one SWAP:
    if not more:
        return ix
    # Create an array with elements {0, 1, 2, ..., depth - 1}:
    stack = list(range(depth))
    # Simulate the combined effect of these instructions by "running" them on
    # our "stack":
    for i in range(length):
        if instrs[ix + i].i_opcode == SWAP:
            oparg = instrs[ix + i].i_oparg
            top = stack[0]
            # SWAPs are 1-indexed:
            stack[0] = stack[oparg - 1]
            stack[oparg - 1] = top
    # Now we can begin! Our approach here is based on a solution to a closely
    # related problem (https://cs.stackexchange.com/a/13938). It's easiest to
    # think of this algorithm as determining the steps needed to efficiently
    # "un-shuffle" our stack. By performing the moves in *reverse* order,
    # though, we can efficiently *shuffle* it! For this reason, we will be
    # replacing instructions starting from the *end* of the run. Since the
    # solution is optimal, we don't need to worry about running out of space:
    current = length - 1
    for i in range(depth):
        # Skip items that have already been visited, or just happen to be in
        # the correct location:
        if stack[i] == VISITED or stack[i] == i:
            continue
        # Okay, we've found an item that hasn't been visited. It forms a cycle
        # with other items; traversing the cycle and swapping each item with
        # the next will put them all in the correct place. The weird
        # loop-and-a-half is necessary to insert 0 into every cycle, since we
        # can only swap from that position:
        j = i
        while True:
            # Skip the actual swap if our item is zero, since swapping the top
            # item with itself is pointless:
            if j:
                assert 0 <= current
                # SWAPs are 1-indexed:
                instrs[ix + current].i_opcode = SWAP
                instrs[ix + current].i_oparg = j + 1
                current -= 1
            if stack[j] == VISITED:
                # Completed the cycle:
                assert j == i
                break
            next_j = stack[j]
            stack[j] = VISITED
            j = next_j
    # NOP out any unused instructions:
    while 0 <= current:
        instrs[ix + current].set_op0(NOP)
        current -= 1
    return ix + length - 1


# This list is pretty small, since it's only okay to reorder opcodes that:
# - can't affect control flow (like jumping or raising exceptions)
# - can't invoke arbitrary code (besides finalizers)
# - only touch the TOS (and pop it when finished)
def swappable(opcode):
    return opcode == STORE_FAST or opcode == STORE_FAST_MAYBE_NULL or opcode == POP_TOP


def stores_to(instr):
    return instr.i_oparg if instr.i_opcode in (STORE_FAST, STORE_FAST_MAYBE_NULL) else -1


def next_swappable_instruction(block, i, lineno):
    while True:
        i += 1
        if i >= block.b_iused:
            return -1
        instruction = block.b_instr[i]
        if 0 <= lineno and instruction.i_loc.lineno != lineno:
            # Optimizing across this instruction could cause user-visible
            # changes in the names bound between line tracing events!
            return -1
        if instruction.i_opcode == NOP:
            continue
        if swappable(instruction.i_opcode):
            return i
        return -1


def apply_static_swaps(block, i):
    """Attempt to apply SWAPs statically by swapping *instructions* rather than
    stack items. For example, we can replace SWAP(2), POP_TOP, STORE_FAST(42)
    with the more efficient NOP, STORE_FAST(42), POP_TOP.
    """
    instrs = block.b_instr
    # SWAPs are to our left, and potential swaperands are to our right:
    while 0 <= i:
        assert i < block.b_iused
        swap = instrs[i]
        if swap.i_opcode != SWAP:
            if swap.i_opcode == NOP or swappable(swap.i_opcode):
                # Nope, but we know how to handle these. Keep looking:
                i -= 1
                continue
            # We can't reason about what this instruction does. Bail:
            return
        j = next_swappable_instruction(block, i, -1)
        if j < 0:
            return
        k = j
        lineno = instrs[j].i_loc.lineno
        for _ in range(swap.i_oparg - 1, 0, -1):
            k = next_swappable_instruction(block, k, lineno)
            if k < 0:
                return
        # The reordering is not safe if the two instructions to be swapped
        # store to the same location, or if any intervening instruction stores
        # to the same location as either of them.
        store_j = stores_to(instrs[j])
        store_k = stores_to(instrs[k])
        if store_j >= 0 or store_k >= 0:
            if store_j == store_k:
                return
            for idx in range(j + 1, k):
                store_idx = stores_to(instrs[idx])
                if store_idx >= 0 and (store_idx == store_j or store_idx == store_k):
                    return
        # Success!
        swap.set_op0(NOP)
        instrs[j], instrs[k] = instrs[k], instrs[j]
        i -= 1


def basicblock_optimize_load_const(const_cache, bb, consts, consts_index):
    opcode = 0
    oparg = 0
    instrs = bb.b_instr
    for i in range(bb.b_iused):
        inst = instrs[i]
        if inst.i_opcode == LOAD_CONST:
            constant = get_const_value(inst.i_opcode, inst.i_oparg, consts)
            maybe_instr_make_load_smallint(inst, constant, consts, const_cache)
        is_copy_of_load_const = (opcode == LOAD_CONST and inst.i_opcode == COPY and inst.i_oparg == 1)
        if not is_copy_of_load_const:
            opcode = inst.i_opcode
            oparg = inst.i_oparg
        assert not is_assembler_opcode(opcode)
        if opcode != LOAD_CONST and opcode != LOAD_SMALL_INT:
            continue
        nextop = instrs[i + 1].i_opcode if i + 1 < bb.b_iused else 0
        if nextop in (POP_JUMP_IF_FALSE, POP_JUMP_IF_TRUE, JUMP_IF_FALSE, JUMP_IF_TRUE):
            # Remove LOAD_CONST const; conditional jump
            cnt = get_const_value(opcode, oparg, consts)
            is_true = bool(cnt)
            if opcode_stack_effect(nextop, 0) == -1:
                # POP_JUMP_IF_FALSE or POP_JUMP_IF_TRUE
                inst.set_op0(NOP)
            jump_if_true = nextop == POP_JUMP_IF_TRUE or nextop == JUMP_IF_TRUE
            if is_true == jump_if_true:
                instrs[i + 1].i_opcode = JUMP
            else:
                instrs[i + 1].set_op0(NOP)
        elif nextop == IS_OP:
            # Fold to POP_JUMP_IF_NONE:
            # - LOAD_CONST(None) IS_OP(0) POP_JUMP_IF_TRUE
            # - LOAD_CONST(None) IS_OP(1) POP_JUMP_IF_FALSE
            # - LOAD_CONST(None) IS_OP(0) TO_BOOL POP_JUMP_IF_TRUE
            # - LOAD_CONST(None) IS_OP(1) TO_BOOL POP_JUMP_IF_FALSE
            # Fold to POP_JUMP_IF_NOT_NONE:
            # - LOAD_CONST(None) IS_OP(0) POP_JUMP_IF_FALSE
            # - LOAD_CONST(None) IS_OP(1) POP_JUMP_IF_TRUE
            # - LOAD_CONST(None) IS_OP(0) TO_BOOL POP_JUMP_IF_FALSE
            # - LOAD_CONST(None) IS_OP(1) TO_BOOL POP_JUMP_IF_TRUE
            cnt = get_const_value(opcode, oparg, consts)
            if cnt is not None:
                continue
            if bb.b_iused <= i + 2:
                continue
            is_instr = instrs[i + 1]
            jump_instr = instrs[i + 2]
            # Get rid of TO_BOOL regardless:
            if jump_instr.i_opcode == TO_BOOL:
                jump_instr.set_op0(NOP)
                if bb.b_iused <= i + 3:
                    continue
                jump_instr = instrs[i + 3]
            invert = bool(is_instr.i_oparg)
            if jump_instr.i_opcode == POP_JUMP_IF_FALSE:
                invert = not invert
            elif jump_instr.i_opcode != POP_JUMP_IF_TRUE:
                continue
            inst.set_op0(NOP)
            is_instr.set_op0(NOP)
            jump_instr.i_opcode = POP_JUMP_IF_NOT_NONE if invert else POP_JUMP_IF_NONE
        elif nextop == TO_BOOL:
            cnt = get_const_value(opcode, oparg, consts)
            index = add_const(bool(cnt), consts, const_cache, consts_index)
            inst.set_op0(NOP)
            instrs[i + 1].set_op1(LOAD_CONST, index)


def optimize_load_const(const_cache, g, consts, consts_index):
    for b in blocks(g.g_entryblock):
        basicblock_optimize_load_const(const_cache, b, consts, consts_index)


_NOP_INSTR = CfgInstr(NOP, 0, NO_LOCATION)


def optimize_basic_block(const_cache, bb, consts, consts_index):
    instrs = bb.b_instr
    i = 0
    while i < bb.b_iused:
        inst = instrs[i]
        opcode = inst.i_opcode
        oparg = inst.i_oparg
        if has_target(opcode):
            assert inst.i_target.b_iused > 0
            target = inst.i_target.b_instr[0]
            assert not is_assembler_opcode(target.i_opcode)
        else:
            target = _NOP_INSTR
        nextop = instrs[i + 1].i_opcode if i + 1 < bb.b_iused else 0
        assert not is_assembler_opcode(opcode)
        if opcode == BUILD_TUPLE:
            # Try to fold tuples of constants.
            # Skip over BUILD_TUPLE(1) UNPACK_SEQUENCE(1).
            # Replace BUILD_TUPLE(2) UNPACK_SEQUENCE(2) with SWAP(2).
            # Replace BUILD_TUPLE(3) UNPACK_SEQUENCE(3) with SWAP(3).
            if nextop == UNPACK_SEQUENCE and oparg == instrs[i + 1].i_oparg:
                if oparg == 1:
                    inst.set_op0(NOP)
                    instrs[i + 1].set_op0(NOP)
                    i += 1
                    continue
                if oparg in (2, 3):
                    inst.set_op0(NOP)
                    instrs[i + 1].i_opcode = SWAP
                    i += 1
                    continue
            fold_tuple_of_constants(bb, i, consts, const_cache, consts_index)
        elif opcode in (BUILD_LIST, BUILD_SET):
            optimize_lists_and_sets(bb, i, nextop, consts, const_cache, consts_index)
        elif opcode in (POP_JUMP_IF_NOT_NONE, POP_JUMP_IF_NONE):
            if target.i_opcode == JUMP:
                i -= jump_thread(bb, inst, target, inst.i_opcode)
        elif opcode == POP_JUMP_IF_FALSE:
            if target.i_opcode == JUMP:
                i -= jump_thread(bb, inst, target, POP_JUMP_IF_FALSE)
        elif opcode == POP_JUMP_IF_TRUE:
            if target.i_opcode == JUMP:
                i -= jump_thread(bb, inst, target, POP_JUMP_IF_TRUE)
        elif opcode in (JUMP_IF_FALSE, JUMP_IF_TRUE):
            same, other = (JUMP_IF_FALSE, JUMP_IF_TRUE) if opcode == JUMP_IF_FALSE else (JUMP_IF_TRUE, JUMP_IF_FALSE)
            if target.i_opcode in (JUMP, same):
                i -= jump_thread(bb, inst, target, same)
                i += 1
                continue
            if target.i_opcode == other:
                # No need to check for loops here, a block's b_next
                # cannot point to itself.
                assert inst.i_target is not inst.i_target.b_next
                inst.i_target = inst.i_target.b_next
                continue  # i-- then the loop's i++
        elif opcode in (JUMP, JUMP_NO_INTERRUPT):
            if target.i_opcode == JUMP:
                i -= jump_thread(bb, inst, target, JUMP)
                i += 1
                continue
            if target.i_opcode == JUMP_NO_INTERRUPT:
                i -= jump_thread(bb, inst, target, opcode)
                i += 1
                continue
        elif opcode == FOR_ITER:
            # The jump at target could be forward or backward and FOR_ITER
            # only jumps forward, so jumps to jumps are not threaded here.
            pass
        elif opcode == STORE_FAST:
            if (opcode == nextop and oparg == instrs[i + 1].i_oparg and
                    instrs[i].i_loc.lineno == instrs[i + 1].i_loc.lineno):
                instrs[i].i_opcode = POP_TOP
                instrs[i].i_oparg = 0
        elif opcode == SWAP:
            if oparg == 1:
                inst.set_op0(NOP)
        elif opcode == LOAD_GLOBAL:
            if nextop == PUSH_NULL and (oparg & 1) == 0:
                inst.set_op1(LOAD_GLOBAL, oparg | 1)
                instrs[i + 1].set_op0(NOP)
        elif opcode == COMPARE_OP:
            if nextop == TO_BOOL:
                inst.set_op0(NOP)
                instrs[i + 1].set_op1(COMPARE_OP, oparg | 16)
                i += 1
                continue
        elif opcode in (CONTAINS_OP, IS_OP):
            if nextop == TO_BOOL:
                inst.set_op0(NOP)
                instrs[i + 1].set_op1(opcode, oparg)
                i += 1
                continue
            if nextop == UNARY_NOT:
                inst.set_op0(NOP)
                inverted = oparg ^ 1
                assert inverted == 0 or inverted == 1
                instrs[i + 1].set_op1(opcode, inverted)
                i += 1
                continue
        elif opcode == TO_BOOL:
            if nextop == TO_BOOL:
                inst.set_op0(NOP)
                i += 1
                continue
        elif opcode in (UNARY_NOT, UNARY_INVERT, UNARY_NEGATIVE):
            if opcode == UNARY_NOT:
                if nextop == TO_BOOL:
                    inst.set_op0(NOP)
                    instrs[i + 1].set_op0(UNARY_NOT)
                    i += 1
                    continue
                if nextop == UNARY_NOT:
                    inst.set_op0(NOP)
                    instrs[i + 1].set_op0(NOP)
                    i += 1
                    continue
            fold_const_unaryop(bb, i, consts, const_cache, consts_index)
        elif opcode == CALL_INTRINSIC_1:
            if oparg == INTRINSIC_LIST_TO_TUPLE:
                fold_constant_seq_into_load_const(bb, i, consts, const_cache, consts_index)
                if inst.i_opcode == CALL_INTRINSIC_1 and nextop == GET_ITER:
                    inst.set_op0(NOP)
            elif oparg == INTRINSIC_UNARY_POSITIVE:
                fold_const_unaryop(bb, i, consts, const_cache, consts_index)
        elif opcode in (LIST_APPEND, SET_ADD):
            if oparg == 1 and (nextop == GET_ITER or nextop == CONTAINS_OP):
                fold_constant_seq_into_load_const(bb, i, consts, const_cache, consts_index)
        elif opcode == BINARY_OP:
            fold_const_binop(bb, i, consts, const_cache, consts_index)
        i += 1

    i = 0
    while i < bb.b_iused:
        if instrs[i].i_opcode == SWAP:
            i = swaptimize(bb, i)
            apply_static_swaps(bb, i)
        i += 1


def remove_redundant_nops_and_jumps(g):
    while True:
        # Convergence is guaranteed because the number of
        # redundant jumps and nops only decreases.
        removed_nops = remove_redundant_nops(g)
        removed_jumps = remove_redundant_jumps(g)
        if removed_nops + removed_jumps == 0:
            return


def optimize_cfg(g, consts, const_cache, consts_index, firstlineno):
    """Perform optimizations on a control flow graph.
    The consts object should still be in list form to allow new constants
    to be appended.

    Code trasnformations that reduce code size initially fill the gaps with
    NOPs.  Later those NOPs are removed.
    """
    check_cfg(g)
    inline_small_or_no_lineno_blocks(g.g_entryblock)
    remove_unreachable(g.g_entryblock)
    resolve_line_numbers(g, firstlineno)
    optimize_load_const(const_cache, g, consts, consts_index)
    for b in blocks(g.g_entryblock):
        optimize_basic_block(const_cache, b, consts, consts_index)
    remove_redundant_nops_and_pairs(g.g_entryblock)
    remove_unreachable(g.g_entryblock)
    remove_redundant_nops_and_jumps(g)


def make_super_instruction(inst1, inst2, super_op):
    line1 = inst1.i_loc.lineno
    line2 = inst2.i_loc.lineno
    # Skip if instructions are on different lines
    if line1 >= 0 and line2 >= 0 and line1 != line2:
        return
    if inst1.i_oparg >= 16 or inst2.i_oparg >= 16:
        return
    inst1.set_op1(super_op, (inst1.i_oparg << 4) | inst2.i_oparg)
    inst2.set_op0(NOP)


def insert_superinstructions(g):
    for b in blocks(g.g_entryblock):
        instrs = b.b_instr
        for i in range(b.b_iused):
            inst = instrs[i]
            nextop = instrs[i + 1].i_opcode if i + 1 < b.b_iused else 0
            if inst.i_opcode == LOAD_FAST:
                if nextop == LOAD_FAST:
                    make_super_instruction(inst, instrs[i + 1], LOAD_FAST_LOAD_FAST)
            elif inst.i_opcode == STORE_FAST:
                if nextop == LOAD_FAST:
                    make_super_instruction(inst, instrs[i + 1], STORE_FAST_LOAD_FAST)
                elif nextop == STORE_FAST:
                    make_super_instruction(inst, instrs[i + 1], STORE_FAST_STORE_FAST)
    return remove_redundant_nops(g)


NOT_LOCAL = -1
DUMMY_INSTR = -1

# LoadFastInstrFlag
# The loaded reference is still on the stack when the local is killed
SUPPORT_KILLED = 1
# The loaded reference is stored into a local
STORED_AS_LOCAL = 2
# The loaded reference is still on the stack at the end of the basic block
REF_UNCONSUMED = 4


def kill_local(instr_flags, refs, local):
    for r_instr, r_local in refs:
        if r_local == local:
            assert r_instr >= 0
            instr_flags[r_instr] |= SUPPORT_KILLED


def store_local(instr_flags, refs, local, r):
    kill_local(instr_flags, refs, local)
    if r[0] != DUMMY_INSTR:
        instr_flags[r[0]] |= STORED_AS_LOCAL


def load_fast_push_block(stack, target, start_depth):
    assert target.b_startdepth >= 0 and target.b_startdepth == start_depth
    if not target.b_visited:
        target.b_visited = 1
        stack.append(target)


_CONSUME_NO_INPUTS = frozenset((FORMAT_SIMPLE, GET_ANEXT, GET_LEN, GET_YIELD_FROM_ITER, IMPORT_FROM,
                                MATCH_KEYS, MATCH_MAPPING, MATCH_SEQUENCE, WITH_EXCEPT_START))
_CONSUME_SOME_PUSH_NONE = frozenset((DICT_MERGE, DICT_UPDATE, LIST_APPEND, LIST_EXTEND, MAP_ADD,
                                     RERAISE, SET_ADD, SET_UPDATE))


def optimize_load_fast(g):
    """Strength reduce LOAD_FAST{_LOAD_FAST} instructions into faster variants
    that load borrowed references onto the operand stack.

    This is only safe when we can prove that the reference in the frame outlives
    the borrowed reference produced by the instruction. We make this tractable
    by enforcing the following lifetimes:

    1. Borrowed references loaded onto the operand stack live until the end of
       the instruction that consumes them from the stack. Any borrowed
       references that would escape into the heap (e.g. into frame objects or
       generators) are converted into new, strong references.

    2. Locals live until they are either killed by an instruction
       (e.g. STORE_FAST) or the frame is unwound. Any local that is overwritten
       via `f_locals` is added to a tuple owned by the frame object.

    To simplify the problem of detecting which supporting references in the
    frame are killed by instructions that overwrite locals, we only allow
    borrowed references to be stored as a local in the frame if they were passed
    as an argument. {RETURN,YIELD}_VALUE convert borrowed references into new,
    strong references.

    Using the above, we can optimize any LOAD_FAST{_LOAD_FAST} instructions
    that meet the following criteria:

    1. The produced reference must be consumed from the stack before the
       supporting reference in the frame is killed.

    2. The produced reference cannot be stored as a local.

    We use abstract interpretation to identify instructions that meet these
    criteria. For each basic block, we simulate the effect the bytecode has on a
    stack of abstract references and note any instructions that violate the
    criteria above. Once we've processed all the instructions in a block, any
    non-violating LOAD_FAST{_LOAD_FAST} can be optimized.

    Each abstract reference is a pair (index of the instruction that produced
    it or DUMMY_INSTR, the local it refers to or NOT_LOCAL).
    """
    entryblock = g.g_entryblock
    max_instrs = 0
    for b in blocks(entryblock):
        max_instrs = max(max_instrs, b.b_iused)
    instr_flags = [0] * max_instrs
    stack = make_cfg_traversal_stack(entryblock)
    stack.append(entryblock)
    entryblock.b_startdepth = 0
    entryblock.b_visited = 1
    refs = []

    while stack:
        block = stack.pop()
        assert block.b_startdepth > -1

        # Reset per-block state.
        for i in range(block.b_iused):
            instr_flags[i] = 0

        # Reset the stack of refs. We don't track references on the stack
        # across basic blocks, but the bytecode will expect their
        # presence. Add dummy references as necessary.
        refs.clear()
        for i in range(block.b_startdepth):
            refs.append((DUMMY_INSTR, NOT_LOCAL))

        for i, instr in enumerate(block.used()):
            opcode = instr.i_opcode
            oparg = instr.i_oparg
            assert opcode != EXTENDED_ARG
            # Opcodes that load and store locals
            if opcode == DELETE_FAST:
                kill_local(instr_flags, refs, oparg)
            elif opcode == LOAD_FAST:
                refs.append((i, oparg))
            elif opcode == LOAD_FAST_AND_CLEAR:
                kill_local(instr_flags, refs, oparg)
                refs.append((i, oparg))
            elif opcode == LOAD_FAST_LOAD_FAST:
                refs.append((i, oparg >> 4))
                refs.append((i, oparg & 15))
            elif opcode == STORE_FAST:
                r = refs.pop()
                store_local(instr_flags, refs, oparg, r)
            elif opcode == STORE_FAST_LOAD_FAST:
                # STORE_FAST
                r = refs.pop()
                store_local(instr_flags, refs, oparg >> 4, r)
                # LOAD_FAST
                refs.append((i, oparg & 15))
            elif opcode == STORE_FAST_STORE_FAST:
                # STORE_FAST
                r = refs.pop()
                store_local(instr_flags, refs, oparg >> 4, r)
                # STORE_FAST
                r = refs.pop()
                store_local(instr_flags, refs, oparg & 15, r)

            # Opcodes that shuffle values on the stack
            elif opcode == COPY:
                assert oparg > 0
                refs.append(refs[len(refs) - oparg])
            elif opcode == SWAP:
                assert oparg >= 2
                idx = len(refs) - oparg
                refs[idx], refs[-1] = refs[-1], refs[idx]

            # We treat opcodes that do not consume all of their inputs on
            # a case by case basis, as we have no generic way of knowing
            # how many inputs should be left on the stack.

            # Opcodes that consume no inputs
            elif opcode in _CONSUME_NO_INPUTS:
                net_pushed = opcodes.num_pushed(opcode, oparg) - opcodes.num_popped(opcode, oparg)
                assert net_pushed >= 0
                # The C pushes the loop counter as the producing instruction.
                for j in range(net_pushed):
                    refs.append((j, NOT_LOCAL))

            # Opcodes that consume some inputs and push no new values
            elif opcode in _CONSUME_SOME_PUSH_NONE:
                net_popped = opcodes.num_popped(opcode, oparg) - opcodes.num_pushed(opcode, oparg)
                assert net_popped > 0
                del refs[len(refs) - net_popped:]

            elif opcode in (END_SEND, SET_FUNCTION_ATTRIBUTE):
                tos = refs.pop()
                refs.pop()
                refs.append(tos)

            # Opcodes that consume some inputs and push new values
            elif opcode == CHECK_EXC_MATCH:
                refs.pop()
                refs.append((i, NOT_LOCAL))
            elif opcode == FOR_ITER:
                load_fast_push_block(stack, instr.i_target, len(refs) + 1)
                refs.append((i, NOT_LOCAL))
            elif opcode in (LOAD_ATTR, LOAD_SUPER_ATTR):
                self_ = refs.pop()
                if opcode == LOAD_SUPER_ATTR:
                    refs.pop()
                    refs.pop()
                refs.append((i, NOT_LOCAL))
                if oparg & 1:
                    # A method call; conservatively assume that self is pushed
                    # back onto the stack
                    refs.append(self_)
            elif opcode in (LOAD_SPECIAL, PUSH_EXC_INFO):
                tos = refs.pop()
                refs.append((i, NOT_LOCAL))
                refs.append(tos)
            elif opcode == SEND:
                load_fast_push_block(stack, instr.i_target, len(refs))
                refs.pop()
                refs.append((i, NOT_LOCAL))

            # Opcodes that consume all of their inputs
            else:
                num_popped = opcodes.num_popped(opcode, oparg)
                num_pushed = opcodes.num_pushed(opcode, oparg)
                if has_target(instr.i_opcode):
                    load_fast_push_block(stack, instr.i_target, len(refs) - num_popped + num_pushed)
                if not is_block_push_opcode(instr.i_opcode):
                    # Block push opcodes only affect the stack when jumping
                    # to the target.
                    for _ in range(num_popped):
                        refs.pop()
                    for _ in range(num_pushed):
                        refs.append((i, NOT_LOCAL))

        # Push fallthrough block
        term = basicblock_last_instr(block)
        if (term is not None and block.b_next is not None and
                not (is_unconditional_jump_opcode(term.i_opcode) or is_scope_exit_opcode(term.i_opcode))):
            assert bb_has_fallthrough(block)
            load_fast_push_block(stack, block.b_next, len(refs))

        # Mark instructions that produce values that are on the stack at the
        # end of the basic block
        for r_instr, _ in refs:
            if r_instr != -1:
                instr_flags[r_instr] |= REF_UNCONSUMED

        # Optimize instructions
        for i, instr in enumerate(block.used()):
            if not instr_flags[i]:
                if instr.i_opcode == LOAD_FAST:
                    instr.i_opcode = LOAD_FAST_BORROW
                elif instr.i_opcode == LOAD_FAST_LOAD_FAST:
                    instr.i_opcode = LOAD_FAST_BORROW_LOAD_FAST_BORROW


# helper functions for add_checks_for_loads_of_unknown_variables

def maybe_push(b, unsafe_mask, stack):
    # Push b if the unsafe mask is giving us any new information.
    # To avoid overflowing the stack, only allow each block once.
    # Use b->b_visited=1 to mean that b is currently on the stack.
    both = b.b_unsafe_locals_mask | unsafe_mask
    if b.b_unsafe_locals_mask != both:
        b.b_unsafe_locals_mask = both
        # More work left to do.
        if not b.b_visited:
            # not on the stack, so push it.
            stack.append(b)
            b.b_visited = 1


def scan_block_for_locals(b, stack):
    # bit i is set if local i is potentially uninitialized
    unsafe_mask = b.b_unsafe_locals_mask
    for instr in b.used():
        assert instr.i_opcode != EXTENDED_ARG
        if instr.i_except is not None:
            maybe_push(instr.i_except, unsafe_mask, stack)
        if instr.i_oparg >= 64:
            continue
        assert instr.i_oparg >= 0
        bit = 1 << instr.i_oparg
        opcode = instr.i_opcode
        if opcode in (DELETE_FAST, LOAD_FAST_AND_CLEAR, STORE_FAST_MAYBE_NULL):
            unsafe_mask |= bit
        elif opcode == STORE_FAST:
            unsafe_mask &= ~bit
        elif opcode == LOAD_FAST_CHECK:
            # If this doesn't raise, then the local is defined.
            unsafe_mask &= ~bit
        elif opcode == LOAD_FAST:
            if unsafe_mask & bit:
                instr.i_opcode = LOAD_FAST_CHECK
            unsafe_mask &= ~bit
    if b.b_next and bb_has_fallthrough(b):
        maybe_push(b.b_next, unsafe_mask, stack)
    last = basicblock_last_instr(b)
    if last and is_jump(last):
        assert last.i_target is not None
        maybe_push(last.i_target, unsafe_mask, stack)


def fast_scan_many_locals(entryblock, nlocals):
    assert nlocals > 64
    states = [0] * (nlocals - 64)
    blocknum = 0
    # state[i - 64] == blocknum if local i is guaranteed to
    # be initialized, i.e., if it has had a previous LOAD_FAST or
    # STORE_FAST within that basicblock (not followed by
    # DELETE_FAST/LOAD_FAST_AND_CLEAR/STORE_FAST_MAYBE_NULL).
    for b in blocks(entryblock):
        blocknum += 1
        for instr in b.used():
            assert instr.i_opcode != EXTENDED_ARG
            arg = instr.i_oparg
            if arg < 64:
                continue
            opcode = instr.i_opcode
            if opcode in (DELETE_FAST, LOAD_FAST_AND_CLEAR, STORE_FAST_MAYBE_NULL):
                states[arg - 64] = blocknum - 1
            elif opcode == STORE_FAST:
                states[arg - 64] = blocknum
            elif opcode == LOAD_FAST:
                if states[arg - 64] != blocknum:
                    instr.i_opcode = LOAD_FAST_CHECK
                states[arg - 64] = blocknum


def remove_unused_consts(entryblock, consts):
    nconsts = len(consts)
    if nconsts == 0:
        return  # nothing to do

    index_map = [-1] * nconsts
    # The first constant may be docstring; keep it always.
    index_map[0] = 0

    # mark used consts
    for b in blocks(entryblock):
        for instr in b.used():
            if opcode_has_const(instr.i_opcode):
                index_map[instr.i_oparg] = instr.i_oparg
    # now index_map[i] == i if consts[i] is used, -1 otherwise
    # condense consts
    used = [i for i in index_map if i != -1]
    n_used_consts = len(used)
    if n_used_consts == nconsts:
        return  # nothing to do

    # move all used consts to the beginning of the consts list
    consts[:] = [consts[old_index] for old_index in used]

    # adjust const indices in the bytecode
    reverse_index_map = [-1] * nconsts
    for i, old_index in enumerate(used):
        reverse_index_map[old_index] = i
    for b in blocks(entryblock):
        for instr in b.used():
            if opcode_has_const(instr.i_opcode):
                index = instr.i_oparg
                assert 0 <= reverse_index_map[index] < n_used_consts
                instr.i_oparg = reverse_index_map[index]


def add_checks_for_loads_of_uninitialized_variables(entryblock, nlocals, nparams):
    if nlocals == 0:
        return
    if nlocals > 64:
        # To avoid O(nlocals**2) compilation, locals beyond the first
        # 64 are only analyzed one basicblock at a time: initialization
        # info is not passed between basicblocks.
        fast_scan_many_locals(entryblock, nlocals)
        nlocals = 64
    stack = make_cfg_traversal_stack(entryblock)

    # First origin of being uninitialized:
    # The non-parameter locals in the entry block.
    start_mask = 0
    for i in range(nparams, nlocals):
        start_mask |= 1 << i
    maybe_push(entryblock, start_mask, stack)

    # Second origin of being uninitialized:
    # There could be DELETE_FAST somewhere, so
    # be sure to scan each basicblock at least once.
    for b in blocks(entryblock):
        scan_block_for_locals(b, stack)
    # Now propagate the uncertainty from the origins we found: Use
    # LOAD_FAST_CHECK for any LOAD_FAST where the local could be undefined.
    while stack:
        b = stack.pop()
        # mark as no longer on stack
        b.b_visited = 0
        scan_block_for_locals(b, stack)


def mark_warm(entryblock):
    stack = make_cfg_traversal_stack(entryblock)
    stack.append(entryblock)
    entryblock.b_visited = 1
    while stack:
        b = stack.pop()
        assert not b.b_except_handler
        b.b_warm = 1
        next_ = b.b_next
        if next_ and bb_has_fallthrough(b) and not next_.b_visited:
            stack.append(next_)
            next_.b_visited = 1
        for instr in b.used():
            if is_jump(instr) and not instr.i_target.b_visited:
                stack.append(instr.i_target)
                instr.i_target.b_visited = 1


def mark_cold(entryblock):
    for b in blocks(entryblock):
        assert not b.b_cold and not b.b_warm
    mark_warm(entryblock)

    stack = make_cfg_traversal_stack(entryblock)
    for b in blocks(entryblock):
        if b.b_except_handler:
            assert not b.b_warm
            stack.append(b)
            b.b_visited = 1

    while stack:
        b = stack.pop()
        b.b_cold = 1
        next_ = b.b_next
        if next_ and bb_has_fallthrough(b):
            if not next_.b_warm and not next_.b_visited:
                stack.append(next_)
                next_.b_visited = 1
        for i, instr in enumerate(b.used()):
            if is_jump(instr):
                assert i == b.b_iused - 1
                target = instr.i_target
                if not target.b_warm and not target.b_visited:
                    stack.append(target)
                    target.b_visited = 1


def push_cold_blocks_to_end(g):
    entryblock = g.g_entryblock
    if entryblock.b_next is None:
        # single basicblock, no need to reorder
        return
    mark_cold(entryblock)

    next_lbl = get_max_label(g.g_entryblock) + 1

    # If we have a cold block with fallthrough to a warm block, add
    # an explicit jump instead of fallthrough
    for b in blocks(entryblock):
        if b.b_cold and bb_has_fallthrough(b) and b.b_next and b.b_next.b_warm:
            explicit_jump = cfg_builder_new_block(g)
            if not is_label(b.b_next.b_label):
                b.b_next.b_label = next_lbl
                next_lbl += 1
            basicblock_addop(explicit_jump, JUMP_NO_INTERRUPT, b.b_next.b_label, NO_LOCATION)
            explicit_jump.b_cold = 1
            explicit_jump.b_next = b.b_next
            explicit_jump.b_predecessors = 1
            b.b_next = explicit_jump

            # set target
            last = basicblock_last_instr(explicit_jump)
            last.i_target = explicit_jump.b_next

    assert not entryblock.b_cold  # First block can't be cold
    cold_blocks = None
    cold_blocks_tail = None

    b = entryblock
    while b.b_next:
        assert not b.b_cold
        while b.b_next and not b.b_next.b_cold:
            b = b.b_next
        if b.b_next is None:
            # no more cold blocks
            break

        # b->b_next is the beginning of a cold streak
        assert not b.b_cold and b.b_next.b_cold

        b_end = b.b_next
        while b_end.b_next and b_end.b_next.b_cold:
            b_end = b_end.b_next

        # b_end is the end of the cold streak
        assert b_end and b_end.b_cold
        assert b_end.b_next is None or not b_end.b_next.b_cold

        if cold_blocks is None:
            cold_blocks = b.b_next
        else:
            cold_blocks_tail.b_next = b.b_next
        cold_blocks_tail = b_end
        b.b_next = b_end.b_next
        b_end.b_next = None
    assert b is not None and b.b_next is None
    b.b_next = cold_blocks

    if cold_blocks is not None:
        remove_redundant_nops_and_jumps(g)


def convert_pseudo_conditional_jumps(g):
    for b in blocks(g.g_entryblock):
        i = 0
        while i < b.b_iused:
            instr = b.b_instr[i]
            if instr.i_opcode == JUMP_IF_FALSE or instr.i_opcode == JUMP_IF_TRUE:
                assert i == b.b_iused - 1
                instr.i_opcode = POP_JUMP_IF_FALSE if instr.i_opcode == JUMP_IF_FALSE else POP_JUMP_IF_TRUE
                loc = instr.i_loc
                except_ = instr.i_except
                basicblock_insert_instruction(b, i, CfgInstr(COPY, 1, loc, None, except_))
                i += 1
                basicblock_insert_instruction(b, i, CfgInstr(TO_BOOL, 0, loc, None, except_))
                i += 1
            i += 1


def convert_pseudo_ops(g):
    for b in blocks(g.g_entryblock):
        for instr in b.used():
            if is_block_push(instr):
                instr.set_op0(NOP)
            elif instr.i_opcode == LOAD_CLOSURE:
                assert opcodes.is_pseudo_target(LOAD_CLOSURE, LOAD_FAST)
                instr.i_opcode = LOAD_FAST
            elif instr.i_opcode == STORE_FAST_MAYBE_NULL:
                assert opcodes.is_pseudo_target(STORE_FAST_MAYBE_NULL, STORE_FAST)
                instr.i_opcode = STORE_FAST
    remove_redundant_nops_and_jumps(g)


def is_exit_or_eval_check_without_lineno(b):
    if basicblock_exits_scope(b) or basicblock_has_eval_break(b):
        return basicblock_has_no_lineno(b)
    return False


def duplicate_exits_without_lineno(g):
    """PEP 626 mandates that the f_lineno of a frame is correct
    after a frame terminates. It would be prohibitively expensive
    to continuously update the f_lineno field at runtime,
    so we make sure that all exiting instruction (raises and returns)
    have a valid line number, allowing us to compute f_lineno lazily.
    We can do this by duplicating the exit blocks without line number
    so that none have more than one predecessor. We can then safely
    copy the line number from the sole predecessor block.
    """
    next_lbl = get_max_label(g.g_entryblock) + 1

    # Copy all exit blocks without line number that are targets of a jump.
    entryblock = g.g_entryblock
    for b in blocks(entryblock):
        last = basicblock_last_instr(b)
        if last is None:
            continue
        if is_jump(last):
            target = next_nonempty_block(last.i_target)
            if is_exit_or_eval_check_without_lineno(target) and target.b_predecessors > 1:
                new_target = copy_basicblock(g, target)
                new_target.b_instr[0].i_loc = last.i_loc
                last.i_target = new_target
                target.b_predecessors -= 1
                new_target.b_predecessors = 1
                new_target.b_next = target.b_next
                new_target.b_label = next_lbl
                next_lbl += 1
                target.b_next = new_target

    # Any remaining reachable exit blocks without line number can only be reached by
    # fall through, and thus can only have a single predecessor
    for b in blocks(entryblock):
        if bb_has_fallthrough(b) and b.b_next and b.b_iused > 0:
            if is_exit_or_eval_check_without_lineno(b.b_next):
                last = basicblock_last_instr(b)
                assert last is not None
                b.b_next.b_instr[0].i_loc = last.i_loc


def propagate_line_numbers(entryblock):
    """If an instruction has no line number, but it's predecessor in the BB does,
    then copy the line number. If a successor block has no line number, and only
    one predecessor, then inherit the line number.
    This ensures that all exit blocks (with one predecessor) receive a line number.
    Also reduces the size of the line number table,
    but has no impact on the generated line number events.
    """
    for b in blocks(entryblock):
        last = basicblock_last_instr(b)
        if last is None:
            continue

        prev_location = NO_LOCATION
        for instr in b.used():
            if instr.i_loc.lineno == NO_LOCATION.lineno:
                instr.i_loc = prev_location
            else:
                prev_location = instr.i_loc
        if bb_has_fallthrough(b) and b.b_next.b_predecessors == 1:
            if b.b_next.b_iused > 0:
                if b.b_next.b_instr[0].i_loc.lineno == NO_LOCATION.lineno:
                    b.b_next.b_instr[0].i_loc = prev_location
        if is_jump(last):
            target = last.i_target
            # Like the C, this reads the first slot even of an emptied block.
            if target.b_predecessors == 1 and target.b_instr:
                if target.b_instr[0].i_loc.lineno == NO_LOCATION.lineno:
                    target.b_instr[0].i_loc = prev_location


def resolve_line_numbers(g, firstlineno):
    duplicate_exits_without_lineno(g)
    propagate_line_numbers(g.g_entryblock)


def optimize_code_unit(g, consts, const_cache, nlocals, nparams, firstlineno):
    """_PyCfg_OptimizeCodeUnit"""
    assert g.g_entryblock.b_iused > 0
    # Preprocessing
    # Map labels to targets and mark exception handlers
    translate_jump_labels_to_targets(g.g_entryblock)
    mark_except_handlers(g.g_entryblock)
    label_exception_targets(g.g_entryblock)

    # Optimization
    consts_index = {}
    for i, item in enumerate(consts):
        consts_index.setdefault(id(item), i)

    optimize_cfg(g, consts, const_cache, consts_index, firstlineno)

    remove_unused_consts(g.g_entryblock, consts)
    add_checks_for_loads_of_uninitialized_variables(g.g_entryblock, nlocals, nparams)
    insert_superinstructions(g)

    push_cold_blocks_to_end(g)
    resolve_line_numbers(g, firstlineno)


def build_cellfixedoffsets(umd):
    nlocals = len(umd.u_varnames)
    ncellvars = len(umd.u_cellvars)
    nfreevars = len(umd.u_freevars)

    noffsets = ncellvars + nfreevars
    fixed = [nlocals + i for i in range(noffsets)]
    for varname, cellindex in umd.u_cellvars.items():
        varindex = umd.u_varnames.get(varname)
        if varindex is None:
            continue
        fixed[cellindex] = varindex
    return fixed


def is_generator(cf):
    return cf & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)


def insert_prefix_instructions(umd, entryblock, fixed, nfreevars, code_flags):
    assert umd.u_firstlineno > 0

    # Add the generator prefix instructions.
    if is_generator(code_flags):
        # Note that RETURN_GENERATOR + POP_TOP have a net stack effect
        # of 0. This is because RETURN_GENERATOR pushes an element
        # with _PyFrame_StackPush before switching stacks.
        loc = SourceLocation(umd.u_firstlineno, umd.u_firstlineno, -1, -1)
        basicblock_insert_instruction(entryblock, 0, CfgInstr(RETURN_GENERATOR, 0, loc))
        basicblock_insert_instruction(entryblock, 1, CfgInstr(POP_TOP, 0, loc))

    # Set up cells for any variable that escapes, to be put in a closure.
    ncellvars = len(umd.u_cellvars)
    if ncellvars:
        # umd->u_cellvars has the cells out of order so we sort them
        # before adding the MAKE_CELL instructions.  Note that we
        # adjust for arg cells, which come first.
        nvars = ncellvars + len(umd.u_varnames)
        sorted_ = [0] * nvars
        for i in range(ncellvars):
            sorted_[fixed[i]] = i + 1
        i = 0
        ncellsused = 0
        while ncellsused < ncellvars:
            oldindex = sorted_[i] - 1
            i += 1
            if oldindex == -1:
                continue
            # The oparg will get fixed in offset_derefs().
            basicblock_insert_instruction(entryblock, ncellsused, CfgInstr(MAKE_CELL, oldindex, NO_LOCATION))
            ncellsused += 1

    if nfreevars:
        basicblock_insert_instruction(entryblock, 0, CfgInstr(COPY_FREE_VARS, nfreevars, NO_LOCATION))


_DEREF_OPS = frozenset((MAKE_CELL, LOAD_CLOSURE, LOAD_DEREF, STORE_DEREF, DELETE_DEREF, LOAD_FROM_DICT_OR_DEREF))


def fix_cell_offsets(umd, entryblock, fixedmap):
    nlocals = len(umd.u_varnames)
    ncellvars = len(umd.u_cellvars)
    nfreevars = len(umd.u_freevars)
    noffsets = ncellvars + nfreevars

    # First deal with duplicates (arg cells).
    numdropped = 0
    for i in range(noffsets):
        if fixedmap[i] == i + nlocals:
            fixedmap[i] -= numdropped
        else:
            # It was a duplicate (cell/arg).
            numdropped += 1

    # Then update offsets, either relative to locals or by cell2arg.
    for b in blocks(entryblock):
        for inst in b.used():
            # This is called before extended args are generated.
            assert inst.i_opcode != EXTENDED_ARG
            if inst.i_opcode in _DEREF_OPS:
                oldoffset = inst.i_oparg
                assert 0 <= oldoffset < noffsets
                assert fixedmap[oldoffset] >= 0
                inst.i_oparg = fixedmap[oldoffset]

    return numdropped


def prepare_localsplus(umd, g, code_flags):
    nlocals = len(umd.u_varnames)
    ncellvars = len(umd.u_cellvars)
    nfreevars = len(umd.u_freevars)
    nlocalsplus = nlocals + ncellvars + nfreevars
    cellfixedoffsets = build_cellfixedoffsets(umd)

    # This must be called before fix_cell_offsets().
    insert_prefix_instructions(umd, g.g_entryblock, cellfixedoffsets, nfreevars, code_flags)

    numdropped = fix_cell_offsets(umd, g.g_entryblock, cellfixedoffsets)
    nlocalsplus -= numdropped
    return nlocalsplus


def cfg_from_instruction_sequence(seq):
    """_PyCfg_FromInstructionSequence"""
    seq.apply_label_map()
    g = CfgBuilder()
    instrs = seq.s_instrs
    for instr in instrs:
        instr.i_target = 0
    for instr in instrs:
        if has_target(instr.i_opcode):
            assert 0 <= instr.i_oparg < len(instrs)
            instrs[instr.i_oparg].i_target = 1
    offset = 0
    for i, instr in enumerate(instrs):
        if instr.i_opcode == ANNOTATIONS_PLACEHOLDER:
            ann = seq.s_annotations_code
            if ann is not None:
                assert not ann.s_labelmap and ann.s_nested is None
                for ann_instr in ann.s_instrs:
                    assert not has_target(ann_instr.i_opcode)
                    cfg_builder_addop(g, ann_instr.i_opcode, ann_instr.i_oparg, ann_instr.i_loc)
                offset += ann.s_used - 1
            else:
                offset -= 1
            continue
        if instr.i_target:
            cfg_builder_use_label(g, i + offset)
        opcode = instr.i_opcode
        oparg = instr.i_oparg
        if has_target(opcode):
            oparg += offset
        cfg_builder_addop(g, opcode, oparg, instr.i_loc)
    return g


def cfg_to_instruction_sequence(g, seq):
    """_PyCfg_ToInstructionSequence"""
    lbl = 0
    for b in blocks(g.g_entryblock):
        b.b_label = lbl
        lbl += 1
    for b in blocks(g.g_entryblock):
        seq.use_label(b.b_label)
        for instr in b.used():
            if has_target(instr.i_opcode):
                # Set oparg to the label id (it will later be mapped to an offset)
                instr.i_oparg = instr.i_target.b_label
            seq.addop(instr.i_opcode, instr.i_oparg, instr.i_loc)
            hi = seq.s_instrs[-1].i_except_handler_info
            if instr.i_except is not None:
                hi.h_label = instr.i_except.b_label
                hi.h_startdepth = instr.i_except.b_startdepth
                hi.h_preserve_lasti = instr.i_except.b_preserve_lasti
            else:
                hi.h_label = -1
    seq.apply_label_map()


def optimized_cfg_to_instruction_sequence(g, umd, code_flags, seq):
    """_PyCfg_OptimizedCfgToInstructionSequence: returns (stackdepth, nlocalsplus)."""
    convert_pseudo_conditional_jumps(g)

    stackdepth = calculate_stackdepth(g)

    # prepare_localsplus adds instructions for generators that push
    # and pop an item on the stack. This assertion makes sure there
    # is space on the stack for that.
    # It should always be true, because a generator must have at
    # least one expression or call to INTRINSIC_STOPITERATION_ERROR,
    # which requires stackspace.
    assert not (is_generator(code_flags) and stackdepth == 0)

    nlocalsplus = prepare_localsplus(umd, g, code_flags)

    convert_pseudo_ops(g)

    # Order of basic blocks must have been determined by now

    normalize_jumps(g)

    # Can't modify the bytecode after inserting instructions that produce
    # borrowed references.
    optimize_load_fast(g)

    # Can't modify the bytecode after computing jump offsets.
    cfg_to_instruction_sequence(g, seq)
    return stackdepth, nlocalsplus


def jump_labels_to_targets(g):
    """_PyCfg_JumpLabelsToTargets: used by _PyCompile_Assemble to fill in the
    jump and exception targets in a synthetic CFG (which is not the output of
    the builtin compiler).
    """
    translate_jump_labels_to_targets(g.g_entryblock)
    label_exception_targets(g.g_entryblock)


# Exported API functions

def opcode_stack_effect_with_jump(opcode, oparg, jump):
    """PyCompile_OpcodeStackEffectWithJump"""
    effect = get_stack_effects(opcode, oparg, jump)
    return PY_INVALID_STACK_EFFECT if effect is None else effect


def opcode_stack_effect(opcode, oparg):
    """PyCompile_OpcodeStackEffect"""
    effect = get_stack_effects(opcode, oparg, -1)
    return PY_INVALID_STACK_EFFECT if effect is None else effect


def compile_optimize_cfg(seq, consts, nlocals):
    """_PyCompile_OptimizeCfg (the test hook _testinternalcapi.optimize_cfg):
    constructs a CFG from an instruction sequence, optimizes it and converts
    it back to a new instruction sequence.  consts is extended in place.
    """
    if not isinstance(consts, list):
        raise TypeError("consts must be a list")
    const_cache = {}
    g = cfg_from_instruction_sequence(seq)
    try:
        nparams, firstlineno = 0, 1
        optimize_code_unit(g, consts, const_cache, nlocals, nparams, firstlineno)
        calculate_stackdepth(g)
        optimize_load_fast(g)
        res = InstructionSequence()
        cfg_to_instruction_sequence(g, res)
        return res
    finally:
        cfg_builder_free(g)
