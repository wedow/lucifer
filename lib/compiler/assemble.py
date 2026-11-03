"""The assembler: instruction sequence to code object.

Port of Python/assemble.c, with the location and exception table encodings
of Include/internal/pycore_code.h (InternalDocs/code_objects.md,
InternalDocs/exception_handling.md) and the code object construction of
Objects/codeobject.c that assemble.c relies on.
"""
import types

from .consts import const_cache_merge_one
from .instruction_sequence import NO_LOCATION, SourceLocation, is_terminator_opcode, is_backwards_jump_opcode
from .opcodes import *  # noqa: F403
from . import opcodes

NEXT_LOCATION = SourceLocation(-2, -2, -2, -2)

# Include/cpython/code.h
CO_OPTIMIZED = 0x0001
CO_VARARGS = 0x0004
CO_VARKEYWORDS = 0x0008

# Include/internal/pycore_code.h: _PyLocals_Kind
CO_FAST_ARG_POS = 0x02  # pos-only, pos-or-kw, varargs
CO_FAST_ARG_KW = 0x04   # kw-only, pos-or-kw, varkwargs
CO_FAST_ARG_VAR = 0x08  # varargs, varkwargs
CO_FAST_HIDDEN = 0x10
CO_FAST_LOCAL = 0x20
CO_FAST_CELL = 0x40
CO_FAST_FREE = 0x80

# _PyCodeLocationInfoKind
PY_CODE_LOCATION_INFO_SHORT0 = 0  # short forms are 0 to 9
PY_CODE_LOCATION_INFO_ONE_LINE0 = 10  # one line forms are 10 to 12
PY_CODE_LOCATION_INFO_NO_COLUMNS = 13
PY_CODE_LOCATION_INFO_LONG = 14
PY_CODE_LOCATION_INFO_NONE = 15

INT_MAX = (1 << 31) - 1


def instr_size(instr):
    opcode = instr.i_opcode
    oparg = instr.i_oparg
    assert not opcodes.is_pseudo_instr(opcode)
    extended_args = (0xFFFFFF < oparg) + (0xFFFF < oparg) + (0xFF < oparg)
    caches = opcodes.caches.get(opcode, 0)
    return extended_args + 1 + caches


class Assembler:
    def __init__(self, firstlineno):
        self.a_bytecode = bytearray()      # bytecode, two bytes per code unit
        self.a_except_table = bytearray()  # exception table
        # Location Info
        self.a_lineno = firstlineno        # lineno of last emitted instruction
        self.a_linetable = bytearray()     # location info


# Exception table, see InternalDocs/exception_handling.md for the layout

CONTINUATION_BIT = 64


def assemble_emit_exception_table_item(a, value, msb):
    assert (msb | 128) == 128
    assert 0 <= value < (1 << 30)
    write = a.a_except_table.append
    if value >= 1 << 24:
        write((value >> 24) | CONTINUATION_BIT | msb)
        msb = 0
    if value >= 1 << 18:
        write(((value >> 18) & 0x3f) | CONTINUATION_BIT | msb)
        msb = 0
    if value >= 1 << 12:
        write(((value >> 12) & 0x3f) | CONTINUATION_BIT | msb)
        msb = 0
    if value >= 1 << 6:
        write(((value >> 6) & 0x3f) | CONTINUATION_BIT | msb)
        msb = 0
    write((value & 0x3f) | msb)


def assemble_emit_exception_table_entry(a, start, end, handler_offset, handler):
    size = end - start
    assert end > start
    target = handler_offset
    depth = handler.h_startdepth - 1
    if handler.h_preserve_lasti > 0:
        depth -= 1
    assert depth >= 0
    depth_lasti = (depth << 1) | handler.h_preserve_lasti
    assemble_emit_exception_table_item(a, start, 1 << 7)
    assemble_emit_exception_table_item(a, size, 0)
    assemble_emit_exception_table_item(a, target, 0)
    assemble_emit_exception_table_item(a, depth_lasti, 0)


def assemble_exception_table(a, instrs):
    ioffset = 0
    handler_label = -1
    handler = None
    start = -1
    for instr in instrs.s_instrs:
        if instr.i_except_handler_info.h_label != handler_label:
            if handler_label >= 0:
                handler_offset = instrs.s_instrs[handler_label].i_offset
                assemble_emit_exception_table_entry(a, start, ioffset, handler_offset, handler)
            start = ioffset
            handler = instr.i_except_handler_info.copy()
            handler_label = handler.h_label
        ioffset += instr_size(instr)
    if handler_label >= 0:
        handler_offset = instrs.s_instrs[handler_label].i_offset
        assemble_emit_exception_table_entry(a, start, ioffset, handler_offset, handler)


# Code location emitting code. See InternalDocs/code_objects.md for a
# description of the format.

def write_varint(out, val):
    while val >= 64:
        out.append(64 | (val & 63))
        val >>= 6
    out.append(val)


def write_signed_varint(out, val):
    if val < 0:
        uval = ((-val) << 1) | 1
    else:
        uval = val << 1
    write_varint(out, uval)


def write_location_first_byte(a, code, length):
    """write_location_entry_start"""
    assert (code & 15) == code
    a.a_linetable.append(128 | (code << 3) | (length - 1))


def write_location_info_short_form(a, length, column, end_column):
    assert 0 < length <= 8
    column_low_bits = column & 7
    column_group = column >> 3
    assert column < 80
    assert end_column >= column
    assert end_column - column < 16
    write_location_first_byte(a, PY_CODE_LOCATION_INFO_SHORT0 + column_group, length)
    a.a_linetable.append(((column_low_bits << 4) | (end_column - column)) & 255)


def write_location_info_oneline_form(a, length, line_delta, column, end_column):
    assert 0 < length <= 8
    assert 0 <= line_delta < 3
    assert column < 128
    assert end_column < 128
    write_location_first_byte(a, PY_CODE_LOCATION_INFO_ONE_LINE0 + line_delta, length)
    a.a_linetable.append(column & 255)
    a.a_linetable.append(end_column & 255)


def write_location_info_long_form(a, loc, length):
    assert 0 < length <= 8
    write_location_first_byte(a, PY_CODE_LOCATION_INFO_LONG, length)
    write_signed_varint(a.a_linetable, loc.lineno - a.a_lineno)
    assert loc.end_lineno >= loc.lineno
    write_varint(a.a_linetable, loc.end_lineno - loc.lineno)
    write_varint(a.a_linetable, loc.col_offset + 1)
    write_varint(a.a_linetable, loc.end_col_offset + 1)


def write_location_info_none(a, length):
    write_location_first_byte(a, PY_CODE_LOCATION_INFO_NONE, length)


def write_location_info_no_column(a, length, line_delta):
    write_location_first_byte(a, PY_CODE_LOCATION_INFO_NO_COLUMNS, length)
    write_signed_varint(a.a_linetable, line_delta)


def write_location_info_entry(a, loc, isize):
    if loc.lineno == NO_LOCATION.lineno:
        write_location_info_none(a, isize)
        return
    line_delta = loc.lineno - a.a_lineno
    column = loc.col_offset
    end_column = loc.end_col_offset
    if column < 0 or end_column < 0:
        if loc.end_lineno == loc.lineno or loc.end_lineno < 0:
            write_location_info_no_column(a, isize, line_delta)
            a.a_lineno = loc.lineno
            return
    elif loc.end_lineno == loc.lineno:
        if line_delta == 0 and column < 80 and end_column - column < 16 and end_column >= column:
            write_location_info_short_form(a, isize, column, end_column)
            return
        if 0 <= line_delta < 3 and column < 128 and end_column < 128:
            write_location_info_oneline_form(a, isize, line_delta, column, end_column)
            a.a_lineno = loc.lineno
            return
    write_location_info_long_form(a, loc, isize)
    a.a_lineno = loc.lineno


def assemble_emit_location(a, loc, isize):
    if isize == 0:
        return
    while isize > 8:
        write_location_info_entry(a, loc, 8)
        isize -= 8
    write_location_info_entry(a, loc, isize)


def assemble_location_info(a, instrs, firstlineno):
    a.a_lineno = firstlineno
    loc = NO_LOCATION
    s = instrs.s_instrs
    for i in range(len(s) - 1, -1, -1):
        instr = s[i]
        if instr.i_loc == NEXT_LOCATION:
            if is_terminator_opcode(instr.i_opcode):
                instr.i_loc = NO_LOCATION
            else:
                assert i < len(s) - 1
                instr.i_loc = s[i + 1].i_loc
    size = 0
    for instr in s:
        if loc != instr.i_loc:
            assemble_emit_location(a, loc, size)
            loc = instr.i_loc
            size = 0
        size += instr_size(instr)
    assemble_emit_location(a, loc, size)


def write_instr(out, instr, ilen):
    opcode = instr.i_opcode
    assert not opcodes.is_pseudo_instr(opcode)
    oparg = instr.i_oparg
    caches = opcodes.caches.get(opcode, 0)
    n = ilen - caches
    assert 1 <= n <= 4
    for shift in range(8 * (n - 1), 0, -8):
        out.append(EXTENDED_ARG)
        out.append((oparg >> shift) & 0xFF)
    out.append(opcode)
    out.append(oparg & 0xFF)
    out.extend(bytes(2 * caches))  # CACHE entries with oparg 0


def assemble_emit_instr(a, instr):
    write_instr(a.a_bytecode, instr, instr_size(instr))


def assemble_emit(instrs, first_lineno, const_cache):
    a = Assembler(first_lineno)
    for instr in instrs.s_instrs:
        assemble_emit_instr(a, instr)

    assemble_location_info(a, instrs, a.a_lineno)

    assemble_exception_table(a, instrs)

    a.a_except_table = const_cache_merge_one(const_cache, bytes(a.a_except_table))
    a.a_linetable = const_cache_merge_one(const_cache, bytes(a.a_linetable))
    a.a_bytecode = const_cache_merge_one(const_cache, bytes(a.a_bytecode))
    return a


def dict_keys_inorder(d, offset):
    items = [None] * len(d)
    for k, v in d.items():
        assert 0 <= v - offset < len(d)
        items[v - offset] = k
    return tuple(items)


def compute_localsplus_info(umd, nlocalsplus, flags):
    """The localsplus names and kinds, as lists."""
    names = [None] * nlocalsplus
    kinds = [0] * nlocalsplus

    # Set the locals kinds.  Arg vars fill the first portion of the list.
    argvarkinds = (
        (umd.u_posonlyargcount, CO_FAST_ARG_POS),
        (umd.u_argcount, CO_FAST_ARG_POS | CO_FAST_ARG_KW),
        (umd.u_kwonlyargcount, CO_FAST_ARG_KW),
        (int(bool(flags & CO_VARARGS)), CO_FAST_ARG_VAR | CO_FAST_ARG_POS),
        (int(bool(flags & CO_VARKEYWORDS)), CO_FAST_ARG_VAR | CO_FAST_ARG_KW),
        (-1, 0),  # the remaining local vars
    )
    varnames = list(umd.u_varnames.items())
    pos = 0
    max_ = 0
    for count, argkind in argvarkinds:
        max_ = INT_MAX if count < 0 else max_ + count
        while pos < max_ and pos < len(varnames):
            k, offset = varnames[pos]
            pos += 1
            assert 0 <= offset < nlocalsplus
            kind = CO_FAST_LOCAL | argkind
            if k in umd.u_fasthidden:
                kind |= CO_FAST_HIDDEN
            if k in umd.u_cellvars:
                kind |= CO_FAST_CELL
            names[offset] = k
            kinds[offset] = kind
    nlocals = len(umd.u_varnames)

    # This counter mirrors the fix done in fix_cell_offsets().
    numdropped = 0
    cellvar_offset = -1
    for k, v in umd.u_cellvars.items():
        if k in umd.u_varnames:
            # Skip cells that are already covered by locals.
            numdropped += 1
            continue
        cellvar_offset = v + nlocals - numdropped
        assert cellvar_offset < nlocalsplus
        names[cellvar_offset] = k
        kinds[cellvar_offset] = CO_FAST_CELL

    for k, v in umd.u_freevars.items():
        offset = v + nlocals - numdropped
        assert offset < nlocalsplus
        # XXX If the assertion below fails it is most likely because a freevar
        # was added to u_freevars with the wrong index due to not taking into
        # account cellvars already present, see gh-128632.
        assert offset > cellvar_offset
        names[offset] = k
        kinds[offset] = CO_FAST_FREE
    return names, kinds


def makecode(umd, a, const_cache, constslist, maxdepth, nlocalsplus, code_flags, filename):
    names = const_cache_merge_one(const_cache, dict_keys_inorder(umd.u_names, 0))
    consts = const_cache_merge_one(const_cache, tuple(constslist))

    posonlyargcount = umd.u_posonlyargcount
    posorkwargcount = umd.u_argcount
    kwonlyargcount = umd.u_kwonlyargcount

    localsplusnames, localspluskinds = compute_localsplus_info(umd, nlocalsplus, code_flags)
    localsplusnames = const_cache_merge_one(const_cache, tuple(localsplusnames))

    return new_code(
        filename=filename,
        name=umd.u_name,
        qualname=umd.u_qualname if umd.u_qualname is not None else umd.u_name,
        flags=code_flags,
        code=a.a_bytecode,
        firstlineno=umd.u_firstlineno,
        linetable=a.a_linetable,
        consts=consts,
        names=names,
        localsplusnames=localsplusnames,
        localspluskinds=localspluskinds,
        argcount=posonlyargcount + posorkwargcount,
        posonlyargcount=posonlyargcount,
        kwonlyargcount=kwonlyargcount,
        stacksize=maxdepth,
        exceptiontable=a.a_except_table,
    )


# The offset (in code units) of the END_SEND from the SEND in the `yield from` sequence.
END_SEND_OFFSET = 5


def resolve_jump_offsets(instrs):
    """Compute the size of each instruction and fixup jump args.
    Replace instruction index with position in bytecode.
    """
    s = instrs.s_instrs
    for instr in s:
        if opcodes.flags.get(instr.i_opcode, 0) & HAS_JUMP_FLAG:
            instr.i_target = instr.i_oparg

    while True:
        totsize = 0
        for instr in s:
            instr.i_offset = totsize
            totsize += instr_size(instr)
        extended_arg_recompile = False

        offset = 0
        for instr in s:
            isize = instr_size(instr)
            # jump offsets are computed relative to
            # the instruction pointer after fetching
            # the jump instruction.
            offset += isize
            if opcodes.flags.get(instr.i_opcode, 0) & HAS_JUMP_FLAG:
                target = s[instr.i_target]
                instr.i_oparg = target.i_offset
                if instr.i_opcode == END_ASYNC_FOR:
                    # sys.monitoring needs to be able to find the matching END_SEND
                    # but the target is the SEND, so we adjust it here.
                    instr.i_oparg = offset - instr.i_oparg - END_SEND_OFFSET
                elif instr.i_oparg < offset:
                    assert is_backwards_jump_opcode(instr.i_opcode)
                    instr.i_oparg = offset - instr.i_oparg
                else:
                    assert not is_backwards_jump_opcode(instr.i_opcode)
                    instr.i_oparg = instr.i_oparg - offset
                if instr_size(instr) != isize:
                    extended_arg_recompile = True
        # The first loop's instr_size() needs i_oparg, which the second loop
        # computes, so loop until no new EXTENDED_ARGs appear.  Only jumps can
        # grow, so this converges quickly.
        if not extended_arg_recompile:
            return


def resolve_unconditional_jumps(instrs):
    """Resolve directions of unconditional jumps"""
    for i, instr in enumerate(instrs.s_instrs):
        is_forward = instr.i_oparg > i
        if instr.i_opcode == JUMP:
            instr.i_opcode = JUMP_FORWARD if is_forward else JUMP_BACKWARD
        elif instr.i_opcode == JUMP_NO_INTERRUPT:
            instr.i_opcode = JUMP_FORWARD if is_forward else JUMP_BACKWARD_NO_INTERRUPT
        else:
            assert not (opcodes.flags.get(instr.i_opcode, 0) & HAS_JUMP_FLAG and
                        opcodes.is_pseudo_instr(instr.i_opcode))


def make_code_object(umd, const_cache, consts, maxdepth, instrs, nlocalsplus, code_flags, filename):
    """_PyAssemble_MakeCodeObject"""
    instrs.apply_label_map()
    resolve_unconditional_jumps(instrs)
    resolve_jump_offsets(instrs)
    a = assemble_emit(instrs, umd.u_firstlineno, const_cache)
    return makecode(umd, a, const_cache, consts, maxdepth, nlocalsplus, code_flags, filename)


def new_code(*, filename, name, qualname, flags, code, firstlineno, linetable, consts, names,
             localsplusnames, localspluskinds, argcount, posonlyargcount, kwonlyargcount,
             stacksize, exceptiontable):
    """_PyCode_New with the fields of a _PyCodeConstructor.

    On the host, types.CodeType runs _PyCode_New but cannot express this
    constructor: it copies the names tuple, rebuilds localsplusnames from
    varnames/cellvars/freevars, and computes localspluskinds without the
    CO_FAST_ARG_* bits and with its own CO_FAST_HIDDEN rule.  marshal writes
    all three, and shares tuples by identity, so the host code object is
    built by CodeType and then given these exact objects.
    """
    varnames = tuple(n for n, k in zip(localsplusnames, localspluskinds) if k & CO_FAST_LOCAL)
    cellvars = tuple(n for n, k in zip(localsplusnames, localspluskinds) if k & CO_FAST_CELL)
    freevars = tuple(n for n, k in zip(localsplusnames, localspluskinds) if k & CO_FAST_FREE)
    co = types.CodeType(argcount, posonlyargcount, kwonlyargcount, len(varnames), stacksize, flags,
                        code, consts, names, varnames, filename, name, qualname, firstlineno,
                        linetable, exceptiontable, freevars, cellvars)
    _host_set_code_fields(co, names, localsplusnames, localspluskinds)
    return co


def set_localsplusnames(co, names):
    """Replace co_localsplusnames of the code object co, in place.

    codegen_finish_annotations_scope does this (Py_SETREF) to rename the
    __annotate__ parameter; code.replace() would recompute the kinds.  With
    new_code, this is the one in-place code object mutation the compiler
    needs, so Lucifer's kernel must provide it as a primitive.
    """
    offset = _host_code_layout().localsplusnames
    assert len(names) == len(_host_get_field(co, offset))
    _host_set_field(co, offset, names)


def _host_get_field(co, offset):
    import ctypes
    return ctypes.cast(ctypes.c_void_p.from_address(id(co) + offset).value, ctypes.py_object).value


def _host_set_field(co, offset, new):
    """Py_SETREF on a PyObject * field of the host code object."""
    import ctypes
    old = _host_get_field(co, offset)
    if old is new:
        return
    ctypes.pythonapi.Py_IncRef(ctypes.py_object(new))
    ctypes.c_void_p.from_address(id(co) + offset).value = id(new)
    ctypes.pythonapi.Py_DecRef(ctypes.py_object(old))


def _host_set_code_fields(co, names, localsplusnames, localspluskinds):
    """Give the host code object co_names, co_localsplusnames and kinds.

    _PyCode_New interns the items of both name tuples in place; CodeType did
    that to its copies, so the interned items are taken from those.
    """
    import ctypes
    layout = _host_code_layout()
    for field, ours in ((layout.names, names), (layout.localsplusnames, localsplusnames)):
        theirs = _host_get_field(co, field)
        assert theirs == ours
        for i, (o, t) in enumerate(zip(ours, theirs)):
            if o is not t:
                _host_tuple_set_item(ours, i, t)
        del theirs
        _host_set_field(co, field, ours)
    kinds = _host_get_field(co, layout.localspluskinds)
    assert len(kinds) == len(localspluskinds)
    if localspluskinds:
        ctypes.memmove(id(kinds) + bytes.__basicsize__ - 1, bytes(localspluskinds), len(localspluskinds))


def _host_tuple_set_item(t, i, item):
    """PyTuple_SET_ITEM on an existing tuple, as intern_strings does."""
    import ctypes
    slot = ctypes.c_void_p.from_address(id(t) + tuple.__basicsize__ + i * tuple.__itemsize__)
    old = ctypes.cast(slot.value, ctypes.py_object).value
    ctypes.pythonapi.Py_IncRef(ctypes.py_object(item))
    slot.value = id(item)
    ctypes.pythonapi.Py_DecRef(ctypes.py_object(old))


_layout = None


def _host_code_layout():
    """Offsets of co_names, co_localsplusnames and co_localspluskinds in the
    host's PyCodeObject, found from fields the code type exposes: co_names
    directly, and the two localsplus fields just before co_filename
    (Include/cpython/code.h)."""
    global _layout
    if _layout is None:
        import ctypes
        probe = compile("def f(a): return a", "<layout>", "exec").co_consts[0]
        ptr = ctypes.sizeof(ctypes.c_void_p)
        words = [ctypes.c_void_p.from_address(id(probe) + off).value
                 for off in range(0, type(probe).__basicsize__, ptr)]
        filename = words.index(id(probe.co_filename)) * ptr
        _layout = types.SimpleNamespace(
            names=words.index(id(probe.co_names)) * ptr,
            localsplusnames=filename - 2 * ptr,
            localspluskinds=filename - ptr)
        lpn = ctypes.cast(words[_layout.localsplusnames // ptr], ctypes.py_object).value
        assert lpn == ("a",), "unexpected PyCodeObject layout"
    return _layout

