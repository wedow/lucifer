"""The compiler's code generation stage: a sequence of pseudo-instructions from an AST.

Port of Python/codegen.c.  The entry points are codegen_module() for modules
and codegen_expression() for expressions; compile_ast() (_PyAST_Compile, in
codeunit.py) drives them.  Errors raise exceptions, so the C's error returns
and scope cleanup on error are gone.
"""
import _ast

from . import symtable
from .codeunit import (
    COMPILE_FBLOCK_ASYNC_COMPREHENSION_GENERATOR, COMPILE_FBLOCK_ASYNC_WITH,
    COMPILE_FBLOCK_EXCEPTION_GROUP_HANDLER, COMPILE_FBLOCK_EXCEPTION_HANDLER,
    COMPILE_FBLOCK_FINALLY_END, COMPILE_FBLOCK_FINALLY_TRY, COMPILE_FBLOCK_FOR_LOOP,
    COMPILE_FBLOCK_HANDLER_CLEANUP, COMPILE_FBLOCK_INLINED_COMPREHENSION,
    COMPILE_FBLOCK_POP_VALUE, COMPILE_FBLOCK_STOP_ITERATION, COMPILE_FBLOCK_TRY_EXCEPT,
    COMPILE_FBLOCK_WHILE_LOOP, COMPILE_FBLOCK_WITH,
    COMPILE_OP_DEREF, COMPILE_OP_FAST, COMPILE_OP_GLOBAL, COMPILE_OP_NAME,
    COMPILE_SCOPE_ANNOTATIONS, COMPILE_SCOPE_ASYNC_FUNCTION, COMPILE_SCOPE_CLASS,
    COMPILE_SCOPE_COMPREHENSION, COMPILE_SCOPE_FUNCTION, COMPILE_SCOPE_LAMBDA,
    COMPILE_SCOPE_MODULE, STACK_USE_GUIDELINE,
    CodeUnitMetadata, InlinedComprehensionState, clean_doc, compile_ast, dict_add_obj,
)
from .future import CO_FUTURE_ANNOTATIONS, get_docstring
from .instruction_sequence import *  # noqa: F403 -- opcodes, oparg constants and predicates
from .instruction_sequence import NEXT_LOCATION, NO_LABEL, NO_LOCATION, SourceLocation

__all__ = ["compile_ast"]

COMP_GENEXP, COMP_LISTCOMP, COMP_SETCOMP, COMP_DICTCOMP = range(4)

# Include/opcode.h: BINARY_OP opargs
(NB_ADD, NB_AND, NB_FLOOR_DIVIDE, NB_LSHIFT, NB_MATRIX_MULTIPLY, NB_MULTIPLY, NB_REMAINDER,
 NB_OR, NB_POWER, NB_RSHIFT, NB_SUBTRACT, NB_TRUE_DIVIDE, NB_XOR,
 NB_INPLACE_ADD, NB_INPLACE_AND, NB_INPLACE_FLOOR_DIVIDE, NB_INPLACE_LSHIFT,
 NB_INPLACE_MATRIX_MULTIPLY, NB_INPLACE_MULTIPLY, NB_INPLACE_REMAINDER, NB_INPLACE_OR,
 NB_INPLACE_POWER, NB_INPLACE_RSHIFT, NB_INPLACE_SUBTRACT, NB_INPLACE_TRUE_DIVIDE,
 NB_INPLACE_XOR, NB_SUBSCR) = range(27)

# Include/internal/pycore_intrinsics.h
INTRINSIC_PRINT = 1
INTRINSIC_IMPORT_STAR = 2
INTRINSIC_STOPITERATION_ERROR = 3
INTRINSIC_ASYNC_GEN_WRAP = 4
INTRINSIC_UNARY_POSITIVE = 5
INTRINSIC_LIST_TO_TUPLE = 6
INTRINSIC_TYPEVAR = 7
INTRINSIC_PARAMSPEC = 8
INTRINSIC_TYPEVARTUPLE = 9
INTRINSIC_SUBSCRIPT_GENERIC = 10
INTRINSIC_TYPEALIAS = 11
INTRINSIC_PREP_RERAISE_STAR = 1
INTRINSIC_TYPEVAR_WITH_BOUND = 2
INTRINSIC_TYPEVAR_WITH_CONSTRAINTS = 3
INTRINSIC_SET_FUNCTION_TYPE_PARAMS = 4
INTRINSIC_SET_TYPEPARAM_DEFAULT = 5

# Include/internal/pycore_ceval.h: LOAD_SPECIAL opargs
SPECIAL___ENTER__ = 0
SPECIAL___EXIT__ = 1
SPECIAL___AENTER__ = 2
SPECIAL___AEXIT__ = 3

# Include/internal/pycore_object.h
ANNOTATE_FORMAT_VALUE_WITH_FAKE_GLOBALS = 2

# Rich comparison ops (Include/object.h) and their COMPARE_OP masks
# (Include/internal/pycore_code.h)
Py_LT, Py_LE, Py_EQ, Py_NE, Py_GT, Py_GE = range(6)
COMPARISON_UNORDERED = 1
COMPARISON_LESS_THAN = 2
COMPARISON_GREATER_THAN = 4
COMPARISON_EQUALS = 8
COMPARISON_NOT_EQUALS = COMPARISON_UNORDERED | COMPARISON_LESS_THAN | COMPARISON_GREATER_THAN
compare_masks = {
    Py_LT: COMPARISON_LESS_THAN,
    Py_LE: COMPARISON_LESS_THAN | COMPARISON_EQUALS,
    Py_EQ: COMPARISON_EQUALS,
    Py_NE: COMPARISON_NOT_EQUALS,
    Py_GT: COMPARISON_GREATER_THAN,
    Py_GE: COMPARISON_GREATER_THAN | COMPARISON_EQUALS,
}

# Pseudo opcodes resolved by codegen_addop_name
LOAD_METHOD = -1
LOAD_SUPER_METHOD = -2
LOAD_ZERO_SUPER_ATTR = -3
LOAD_ZERO_SUPER_METHOD = -4

Load, Store, Del = _ast.Load, _ast.Store, _ast.Del


def LOCATION(lno, end_lno, col, end_col):
    return SourceLocation(lno, end_lno, col, end_col)


def LOC(x):
    return SourceLocation(x.lineno, x.end_lineno, x.col_offset, x.end_col_offset)


def new_label(c):
    return c.u.u_instr_sequence.new_label()


def use_label(c, lbl):
    c.u.u_instr_sequence.use_label(lbl)


def addop_i(c, loc, opcode, oparg):
    """Add an opcode with an integer argument."""
    assert not is_assembler_opcode(opcode)
    c.u.u_instr_sequence.addop(opcode, oparg, loc)


def addop(c, loc, opcode):
    assert not opcode_has_arg(opcode)
    assert not is_assembler_opcode(opcode)
    c.u.u_instr_sequence.addop(opcode, 0, loc)


def addop_load_const(c, loc, o):
    addop_i(c, loc, LOAD_CONST, c.add_const(o))


def addop_o(c, loc, opcode, d, o):
    """ADDOP_N: an opcode whose oparg indexes o in dict d."""
    assert not opcode_has_const(opcode)  # use addop_load_const
    addop_i(c, loc, opcode, dict_add_obj(d, o))


def addop_name(c, loc, opcode, d, o):
    mangled = c.maybe_mangle(o)
    arg = dict_add_obj(d, mangled)
    if opcode == LOAD_ATTR:
        arg <<= 1
    if opcode == LOAD_METHOD:
        opcode = LOAD_ATTR
        arg <<= 1
        arg |= 1
    if opcode == LOAD_SUPER_ATTR:
        arg <<= 2
        arg |= 2
    if opcode == LOAD_SUPER_METHOD:
        opcode = LOAD_SUPER_ATTR
        arg <<= 2
        arg |= 3
    if opcode == LOAD_ZERO_SUPER_ATTR:
        opcode = LOAD_SUPER_ATTR
        arg <<= 2
    if opcode == LOAD_ZERO_SUPER_METHOD:
        opcode = LOAD_SUPER_ATTR
        arg <<= 2
        arg |= 1
    addop_i(c, loc, opcode, arg)


def addop_j(c, loc, opcode, target):
    assert target != NO_LABEL
    assert has_target(opcode)
    assert not is_assembler_opcode(opcode)
    c.u.u_instr_sequence.addop(opcode, target, loc)


def names(c):
    return c.u.u_metadata.u_names


def visit_seq_expr(c, seq):
    for elt in seq:
        visit_expr(c, elt)


def visit_seq_stmt(c, seq):
    for elt in seq:
        visit_stmt(c, elt)


def codegen_call_exit_with_nones(c, loc):
    addop_load_const(c, loc, None)
    addop_load_const(c, loc, None)
    addop_load_const(c, loc, None)
    addop_i(c, loc, CALL, 3)


def codegen_add_yield_from(c, loc, await_):
    send = new_label(c)
    fail = new_label(c)
    exit = new_label(c)

    use_label(c, send)
    addop_j(c, loc, SEND, exit)
    # Set up a virtual try/except to handle when StopIteration is raised during
    # a close or throw call. The only way YIELD_VALUE raises if they do!
    addop_j(c, loc, SETUP_FINALLY, fail)
    addop_i(c, loc, YIELD_VALUE, 1)
    addop(c, NO_LOCATION, POP_BLOCK)
    addop_i(c, loc, RESUME, RESUME_AFTER_AWAIT if await_ else RESUME_AFTER_YIELD_FROM)
    addop_j(c, loc, JUMP_NO_INTERRUPT, send)

    use_label(c, fail)
    addop(c, loc, CLEANUP_THROW)

    use_label(c, exit)
    addop(c, loc, END_SEND)


def codegen_pop_except_and_reraise(c, loc):
    # Stack contents
    # [exc_info, lasti, exc]            COPY        3
    # [exc_info, lasti, exc, exc_info]  POP_EXCEPT
    # [exc_info, lasti, exc]            RERAISE      1
    # (exception_unwind clears the stack)
    addop_i(c, loc, COPY, 3)
    addop(c, loc, POP_EXCEPT)
    addop_i(c, loc, RERAISE, 1)


def codegen_unwind_fblock(c, loc, info, preserve_tos):
    """Unwind a frame block.  If preserve_tos is true, the TOS before popping
    the blocks will be restored afterwards, unless another return, break or
    continue is found. In which case, the TOS will be popped.

    Returns the location to use from here on (C's *ploc).
    """
    t = info.fb_type
    if t in (COMPILE_FBLOCK_WHILE_LOOP, COMPILE_FBLOCK_EXCEPTION_HANDLER,
             COMPILE_FBLOCK_EXCEPTION_GROUP_HANDLER, COMPILE_FBLOCK_ASYNC_COMPREHENSION_GENERATOR,
             COMPILE_FBLOCK_INLINED_COMPREHENSION, COMPILE_FBLOCK_STOP_ITERATION):
        return loc
    if t == COMPILE_FBLOCK_FOR_LOOP:
        # Pop the iterator
        if preserve_tos:
            addop_i(c, loc, SWAP, 2)
        addop(c, loc, POP_TOP)
        return loc
    if t == COMPILE_FBLOCK_TRY_EXCEPT:
        addop(c, loc, POP_BLOCK)
        return loc
    if t == COMPILE_FBLOCK_FINALLY_TRY:
        # This POP_BLOCK gets the line number of the unwinding statement
        addop(c, loc, POP_BLOCK)
        if preserve_tos:
            c.push_fblock(loc, COMPILE_FBLOCK_POP_VALUE, NO_LABEL, NO_LABEL, None)
        # Emit the finally block
        visit_seq_stmt(c, info.fb_datum)
        if preserve_tos:
            c.pop_fblock(COMPILE_FBLOCK_POP_VALUE, NO_LABEL)
        # The finally block should appear to execute after the statement
        # causing the unwinding, so make the unwinding instruction artificial
        return NO_LOCATION
    if t == COMPILE_FBLOCK_FINALLY_END:
        if preserve_tos:
            addop_i(c, loc, SWAP, 2)
        addop(c, loc, POP_TOP)  # exc_value
        if preserve_tos:
            addop_i(c, loc, SWAP, 2)
        addop(c, loc, POP_BLOCK)
        addop(c, loc, POP_EXCEPT)
        return loc
    if t in (COMPILE_FBLOCK_WITH, COMPILE_FBLOCK_ASYNC_WITH):
        loc = info.fb_loc
        addop(c, loc, POP_BLOCK)
        if preserve_tos:
            addop_i(c, loc, SWAP, 3)
            addop_i(c, loc, SWAP, 2)
        codegen_call_exit_with_nones(c, loc)
        if t == COMPILE_FBLOCK_ASYNC_WITH:
            addop_i(c, loc, GET_AWAITABLE, 2)
            addop_load_const(c, loc, None)
            codegen_add_yield_from(c, loc, 1)
        addop(c, loc, POP_TOP)
        # The exit block should appear to execute after the statement causing
        # the unwinding, so make the unwinding instruction artificial
        return NO_LOCATION
    if t == COMPILE_FBLOCK_HANDLER_CLEANUP:
        if info.fb_datum:
            addop(c, loc, POP_BLOCK)
        if preserve_tos:
            addop_i(c, loc, SWAP, 2)
        addop(c, loc, POP_BLOCK)
        addop(c, loc, POP_EXCEPT)
        if info.fb_datum:
            addop_load_const(c, loc, None)
            codegen_nameop(c, loc, info.fb_datum, Store)
            codegen_nameop(c, loc, info.fb_datum, Del)
        return loc
    if t == COMPILE_FBLOCK_POP_VALUE:
        if preserve_tos:
            addop_i(c, loc, SWAP, 2)
        addop(c, loc, POP_TOP)
        return loc
    raise AssertionError("unreachable")


def codegen_unwind_fblock_stack(c, loc, preserve_tos, want_loop):
    """Unwind the block stack.  If want_loop, stop at the first loop.

    Returns (loc, loop): C's *ploc and *loop.
    """
    top = c.top_fblock()
    if top is None:
        return loc, None
    if top.fb_type == COMPILE_FBLOCK_EXCEPTION_GROUP_HANDLER:
        c.error(loc, "'break', 'continue' and 'return' cannot appear in an except* block")
    if want_loop and top.fb_type in (COMPILE_FBLOCK_WHILE_LOOP, COMPILE_FBLOCK_FOR_LOOP):
        return loc, top
    c.pop_fblock(top.fb_type, top.fb_block)
    loc = codegen_unwind_fblock(c, loc, top, preserve_tos)
    loc, loop = codegen_unwind_fblock_stack(c, loc, preserve_tos, want_loop)
    c.push_fblock(top.fb_loc, top.fb_type, top.fb_block, top.fb_exit, top.fb_datum)
    return loc, loop


def codegen_enter_scope(c, name, scope_type, key, lineno, private, umd):
    c.enter_scope(name, scope_type, key, lineno, private, umd)
    c.set_qualname()
    loc = LOCATION(0 if scope_type == COMPILE_SCOPE_MODULE else lineno, lineno, 0, 0)
    addop_i(c, loc, RESUME, RESUME_AT_FUNC_START)
    if scope_type == COMPILE_SCOPE_MODULE:
        addop(c, loc, ANNOTATIONS_PLACEHOLDER)


def codegen_emit_annotations_prologue(c, loc):
    # if .format > VALUE_WITH_FAKE_GLOBALS: raise NotImplementedError
    assert not c.u.u_ste.ste_has_docstring
    addop_i(c, loc, LOAD_FAST, 0)
    addop_load_const(c, loc, ANNOTATE_FORMAT_VALUE_WITH_FAKE_GLOBALS)
    addop_i(c, loc, COMPARE_OP, (Py_GT << 5) | compare_masks[Py_GT])
    body = new_label(c)
    addop_j(c, loc, POP_JUMP_IF_FALSE, body)
    addop_i(c, loc, LOAD_COMMON_CONSTANT, CONSTANT_NOTIMPLEMENTEDERROR)
    addop_i(c, loc, RAISE_VARARGS, 1)
    use_label(c, body)


def codegen_setup_annotations_scope(c, loc, key, name):
    umd = CodeUnitMetadata(u_posonlyargcount=1)
    codegen_enter_scope(c, name, COMPILE_SCOPE_ANNOTATIONS, key, loc.lineno, None, umd)
    codegen_emit_annotations_prologue(c, loc)


def codegen_finish_annotations_scope(c, loc):
    addop(c, loc, RETURN_VALUE)
    co = c.optimize_and_assemble(1)
    c.exit_scope()
    # We want the parameter to __annotate__ to be named "format" in the
    # signature shown by inspect.signature(), but we need to use a different
    # name (.format) in the symtable; if the name "format" appears in the
    # annotations, it doesn't get clobbered by this name.  This code is
    # essentially: co.co_localsplusnames = ("format", *co.co_localsplusnames[1:])
    co = co.replace(co_varnames=("format",) + co.co_varnames[1:])
    codegen_make_closure(c, loc, co, 0)


def codegen_deferred_annotations_body(c, loc, deferred_anno, conditional_annotation_indices,
                                      scope_type):
    assert len(deferred_anno) == len(conditional_annotation_indices)
    addop_i(c, loc, BUILD_MAP, 0)  # stack now contains <annos>
    for st, idx in zip(deferred_anno, conditional_annotation_indices):
        mangled = c.mangle(st.target.id)
        not_set = new_label(c)
        if idx != -1:
            addop_load_const(c, LOC(st), idx)
            if scope_type == COMPILE_SCOPE_CLASS:
                addop_name(c, LOC(st), LOAD_DEREF, c.u.u_metadata.u_freevars,
                           "__conditional_annotations__")
            else:
                addop_name(c, LOC(st), LOAD_GLOBAL, names(c), "__conditional_annotations__")
            addop_i(c, LOC(st), CONTAINS_OP, 0)
            addop_j(c, LOC(st), POP_JUMP_IF_FALSE, not_set)
        visit_expr(c, st.annotation)
        addop_i(c, LOC(st), COPY, 2)
        addop_load_const(c, LOC(st), mangled)
        # stack now contains <annos> <name> <annos> <value>
        addop(c, loc, STORE_SUBSCR)
        # stack now contains <annos>
        use_label(c, not_set)


def codegen_process_deferred_annotations(c, loc):
    deferred_anno, conditional_annotation_indices = c.deferred_annotations()
    if deferred_anno is None:
        assert conditional_annotation_indices is None
        return
    scope_type = c.u.u_scope_type
    need_separate_block = scope_type == COMPILE_SCOPE_MODULE
    if need_separate_block:
        c.start_annotation_setup()
    # It's possible that ste_annotations_block is set but
    # u_deferred_annotations is not, because the former is still set if there
    # are only non-simple annotations (i.e., annotations for attributes,
    # subscripts, or parenthesized names). However, the reverse should not be
    # possible.
    ste = c.u.u_ste
    assert ste.ste_annotation_block is not None
    key = ste.ste_id + 1
    codegen_setup_annotations_scope(c, loc, key, ste.ste_annotation_block.ste_name)
    codegen_deferred_annotations_body(c, loc, deferred_anno, conditional_annotation_indices,
                                      scope_type)
    codegen_finish_annotations_scope(c, loc)
    codegen_nameop(c, loc, "__annotate_func__" if ste.ste_type == symtable.ClassBlock
                   else "__annotate__", Store)
    if need_separate_block:
        c.end_annotation_setup()


def codegen_expression(c, e):
    """_PyCodegen_Expression: compile an expression."""
    visit_expr(c, e)


def codegen_module(c, loc, stmts, is_interactive):
    """_PyCodegen_Module: compile a sequence of statements, checking for a
    docstring and for annotations."""
    if c.u.u_ste.ste_has_conditional_annotations:
        addop_i(c, loc, BUILD_SET, 0)
        addop_o(c, loc, STORE_NAME, names(c), "__conditional_annotations__")
    codegen_body(c, loc, stmts, is_interactive)


def codegen_body(c, loc, stmts, is_interactive):
    # If from __future__ import annotations is active, every annotated class
    # and module should have __annotations__.  Else __annotate__ is created
    # when necessary.
    ste = c.u.u_ste
    if (c.c_future.ff_features & CO_FUTURE_ANNOTATIONS) and ste.ste_annotations_used:
        addop(c, loc, SETUP_ANNOTATIONS)
    if not stmts:
        return
    first_instr = 0
    if not is_interactive:  # A string literal on REPL prompt is not a docstring
        if ste.ste_has_docstring:
            docstring = get_docstring(stmts)
            assert docstring is not None
            first_instr = 1
            # set docstring
            assert c.c_optimize < 2
            cleandoc = clean_doc(docstring)
            st = stmts[0]
            assert type(st) is _ast.Expr
            addop_load_const(c, LOC(st.value), cleandoc)
            codegen_nameop(c, NO_LOCATION, "__doc__", Store)
    for st in stmts[first_instr:]:
        visit_stmt(c, st)
    # If there are annotations and the future import is not on, we collect
    # the annotations in a separate pass and generate an __annotate__
    # function. See PEP 649.
    if not (c.c_future.ff_features & CO_FUTURE_ANNOTATIONS):
        codegen_process_deferred_annotations(c, loc)


def enter_anonymous_scope(c, mod):
    """_PyCodegen_EnterAnonymousScope."""
    codegen_enter_scope(c, "<module>", COMPILE_SCOPE_MODULE, id(mod), 1, None, None)


def codegen_make_closure(c, loc, co, flags):
    if co.co_freevars:
        # Bypass com_addop_varname because it will generate LOAD_DEREF but
        # LOAD_CLOSURE is needed.
        for name in co.co_freevars:
            arg = c.lookup_arg(co, name)
            addop_i(c, loc, LOAD_CLOSURE, arg)
        flags |= MAKE_FUNCTION_CLOSURE
        addop_i(c, loc, BUILD_TUPLE, len(co.co_freevars))
    addop_load_const(c, loc, co)
    addop(c, loc, MAKE_FUNCTION)
    if flags & MAKE_FUNCTION_CLOSURE:
        addop_i(c, loc, SET_FUNCTION_ATTRIBUTE, MAKE_FUNCTION_CLOSURE)
    if flags & MAKE_FUNCTION_ANNOTATIONS:
        addop_i(c, loc, SET_FUNCTION_ATTRIBUTE, MAKE_FUNCTION_ANNOTATIONS)
    if flags & MAKE_FUNCTION_ANNOTATE:
        addop_i(c, loc, SET_FUNCTION_ATTRIBUTE, MAKE_FUNCTION_ANNOTATE)
    if flags & MAKE_FUNCTION_KWDEFAULTS:
        addop_i(c, loc, SET_FUNCTION_ATTRIBUTE, MAKE_FUNCTION_KWDEFAULTS)
    if flags & MAKE_FUNCTION_DEFAULTS:
        addop_i(c, loc, SET_FUNCTION_ATTRIBUTE, MAKE_FUNCTION_DEFAULTS)


def codegen_decorators(c, decos):
    for d in decos:
        visit_expr(c, d)


def codegen_apply_decorators(c, decos):
    for d in reversed(decos):
        addop_i(c, LOC(d), CALL, 0)


def codegen_kwonlydefaults(c, loc, kwonlyargs, kw_defaults):
    """Push a dict of keyword-only default values; whether one was pushed."""
    default_count = 0
    for arg, default in zip(kwonlyargs, kw_defaults):
        if default is not None:
            default_count += 1
            addop_load_const(c, loc, c.maybe_mangle(arg.arg))
            visit_expr(c, default)
    if default_count:
        addop_i(c, loc, BUILD_MAP, default_count)
        return True
    return False


def codegen_visit_annexpr(c, annotation):
    from .unparse import expr_as_unicode
    addop_load_const(c, LOC(annotation), expr_as_unicode(annotation))


def codegen_argannotation(c, id, annotation, loc):
    """Push the name and value of one annotation; the number pushed (0 or 1)."""
    if annotation is None:
        return 0
    addop_load_const(c, loc, c.maybe_mangle(id))
    if c.c_future.ff_features & CO_FUTURE_ANNOTATIONS:
        codegen_visit_annexpr(c, annotation)
    else:
        if type(annotation) is _ast.Starred:
            # *args: *Ts (where Ts is a TypeVarTuple).
            # Do [annotation_value] = [*Ts].
            # (Note that in theory we could end up here even for an argument
            # other than *args, but in practice the grammar doesn't allow it.)
            visit_expr(c, annotation.value)
            addop_i(c, loc, UNPACK_SEQUENCE, 1)
        else:
            visit_expr(c, annotation)
    return 1


def codegen_argannotations(c, args, loc):
    n = 0
    for arg in args:
        n += codegen_argannotation(c, arg.arg, arg.annotation, loc)
    return n


def codegen_annotations_in_scope(c, loc, args, returns):
    n = codegen_argannotations(c, args.args, loc)
    n += codegen_argannotations(c, args.posonlyargs, loc)
    if args.vararg and args.vararg.annotation:
        n += codegen_argannotation(c, args.vararg.arg, args.vararg.annotation, loc)
    n += codegen_argannotations(c, args.kwonlyargs, loc)
    if args.kwarg and args.kwarg.annotation:
        n += codegen_argannotation(c, args.kwarg.arg, args.kwarg.annotation, loc)
    n += codegen_argannotation(c, "return", returns, loc)
    return n


def codegen_function_annotations(c, loc, args, returns):
    """Push arg annotation names and values.  The expressions are evaluated
    separately from the rest of the source code.  Returns the flags to add to
    the function."""
    ste = symtable.lookup(c.c_st, id(args))
    if ste.ste_annotations_used:
        codegen_setup_annotations_scope(c, loc, id(args), ste.ste_name)
        annotations_len = codegen_annotations_in_scope(c, loc, args, returns)
        addop_i(c, loc, BUILD_MAP, annotations_len)
        codegen_finish_annotations_scope(c, loc)
        return MAKE_FUNCTION_ANNOTATE
    return 0


def codegen_defaults(c, args, loc):
    visit_seq_expr(c, args.defaults)
    addop_i(c, loc, BUILD_TUPLE, len(args.defaults))


def codegen_default_arguments(c, loc, args):
    funcflags = 0
    if args.defaults:
        codegen_defaults(c, args, loc)
        funcflags |= MAKE_FUNCTION_DEFAULTS
    if args.kwonlyargs:
        if codegen_kwonlydefaults(c, loc, args.kwonlyargs, args.kw_defaults):
            funcflags |= MAKE_FUNCTION_KWDEFAULTS
    return funcflags


def codegen_wrap_in_stopiteration_handler(c):
    handler = new_label(c)
    # Insert SETUP_CLEANUP at start
    c.u.u_instr_sequence.insert_instruction(0, SETUP_CLEANUP, handler, NO_LOCATION)
    addop_load_const(c, NO_LOCATION, None)
    addop(c, NO_LOCATION, RETURN_VALUE)
    use_label(c, handler)
    addop_i(c, NO_LOCATION, CALL_INTRINSIC_1, INTRINSIC_STOPITERATION_ERROR)
    addop_i(c, NO_LOCATION, RERAISE, 1)


def codegen_type_param_bound_or_default(c, e, name, key, allow_starred):
    addop_load_const(c, LOC(e), (1,))
    codegen_setup_annotations_scope(c, LOC(e), key, name)
    if allow_starred and type(e) is _ast.Starred:
        visit_expr(c, e.value)
        addop_i(c, LOC(e), UNPACK_SEQUENCE, 1)
    else:
        visit_expr(c, e)
    addop(c, LOC(e), RETURN_VALUE)
    co = c.optimize_and_assemble(1)
    c.exit_scope()
    codegen_make_closure(c, LOC(e), co, MAKE_FUNCTION_DEFAULTS)


def codegen_type_params(c, type_params):
    if not type_params:
        return
    seen_default = False
    for typeparam in type_params:
        loc = LOC(typeparam)
        t = type(typeparam)
        name = typeparam.name
        addop_load_const(c, loc, name)
        if t is _ast.TypeVar:
            if typeparam.bound:
                bound = typeparam.bound
                codegen_type_param_bound_or_default(c, bound, name, id(typeparam), False)
                intrinsic = (INTRINSIC_TYPEVAR_WITH_CONSTRAINTS if type(bound) is _ast.Tuple
                             else INTRINSIC_TYPEVAR_WITH_BOUND)
                addop_i(c, loc, CALL_INTRINSIC_2, intrinsic)
            else:
                addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_TYPEVAR)
            if typeparam.default_value:
                seen_default = True
                codegen_type_param_bound_or_default(c, typeparam.default_value, name,
                                                    id(typeparam) + 1, False)
                addop_i(c, loc, CALL_INTRINSIC_2, INTRINSIC_SET_TYPEPARAM_DEFAULT)
            elif seen_default:
                c.error(loc, f"non-default type parameter '{name}' follows default type parameter")
        else:
            intrinsic = INTRINSIC_TYPEVARTUPLE if t is _ast.TypeVarTuple else INTRINSIC_PARAMSPEC
            addop_i(c, loc, CALL_INTRINSIC_1, intrinsic)
            if typeparam.default_value:
                codegen_type_param_bound_or_default(c, typeparam.default_value, name,
                                                    id(typeparam), t is _ast.TypeVarTuple)
                addop_i(c, loc, CALL_INTRINSIC_2, INTRINSIC_SET_TYPEPARAM_DEFAULT)
                seen_default = True
            elif seen_default:
                c.error(loc, f"non-default type parameter '{name}' follows default type parameter")
        addop_i(c, loc, COPY, 1)
        codegen_nameop(c, loc, name, Store)
    addop_i(c, LOC(type_params[0]), BUILD_TUPLE, len(type_params))


def codegen_emit_function_body(c, body):
    ste = c.u.u_ste
    first_instr = 0
    if ste.ste_has_docstring:
        docstring = get_docstring(body)
        assert docstring is not None
        first_instr = 1
        c.add_const(clean_doc(docstring))
    start = new_label(c)
    use_label(c, start)
    add_stopiteration_handler = ste.ste_coroutine or ste.ste_generator
    if add_stopiteration_handler:
        # codegen_wrap_in_stopiteration_handler will push a block, so we need
        # to account for that
        c.push_fblock(NO_LOCATION, COMPILE_FBLOCK_STOP_ITERATION, start, NO_LABEL, None)
    for st in body[first_instr:]:
        visit_stmt(c, st)
    if add_stopiteration_handler:
        codegen_wrap_in_stopiteration_handler(c)
        c.pop_fblock(COMPILE_FBLOCK_STOP_ITERATION, start)


def codegen_function_body(c, s, is_async, funcflags, firstlineno):
    args = s.args
    scope_type = COMPILE_SCOPE_ASYNC_FUNCTION if is_async else COMPILE_SCOPE_FUNCTION
    umd = CodeUnitMetadata(len(args.args), len(args.posonlyargs), len(args.kwonlyargs))
    codegen_enter_scope(c, s.name, scope_type, id(s), firstlineno, None, umd)
    codegen_emit_function_body(c, s.body)
    co = c.optimize_and_assemble(1)
    c.exit_scope()
    codegen_make_closure(c, LOC(s), co, funcflags)


def codegen_function(c, s, is_async):
    args = s.args
    returns = s.returns
    decos = s.decorator_list
    name = s.name
    type_params = s.type_params

    codegen_decorators(c, decos)
    firstlineno = s.lineno
    if decos:
        firstlineno = decos[0].lineno
    loc = LOC(s)
    is_generic = len(type_params) > 0
    funcflags = codegen_default_arguments(c, loc, args)
    num_typeparam_args = 0
    if is_generic:
        if funcflags & MAKE_FUNCTION_DEFAULTS:
            num_typeparam_args += 1
        if funcflags & MAKE_FUNCTION_KWDEFAULTS:
            num_typeparam_args += 1
        if num_typeparam_args == 2:
            addop_i(c, loc, SWAP, 2)
        umd = CodeUnitMetadata(u_argcount=num_typeparam_args)
        codegen_enter_scope(c, f"<generic parameters of {name}>", COMPILE_SCOPE_ANNOTATIONS,
                            id(type_params), firstlineno, None, umd)
        codegen_type_params(c, type_params)
        for i in range(num_typeparam_args):
            addop_i(c, loc, LOAD_FAST, i)

    funcflags |= codegen_function_annotations(c, loc, args, returns)
    codegen_function_body(c, s, is_async, funcflags, firstlineno)

    if is_generic:
        addop_i(c, loc, SWAP, 2)
        addop_i(c, loc, CALL_INTRINSIC_2, INTRINSIC_SET_FUNCTION_TYPE_PARAMS)
        co = c.optimize_and_assemble(0)
        c.exit_scope()
        codegen_make_closure(c, loc, co, 0)
        if num_typeparam_args > 0:
            addop_i(c, loc, SWAP, num_typeparam_args + 1)
            addop_i(c, loc, CALL, num_typeparam_args - 1)
        else:
            addop(c, loc, PUSH_NULL)
            addop_i(c, loc, CALL, 0)

    codegen_apply_decorators(c, decos)
    codegen_nameop(c, loc, name, Store)


def codegen_set_type_params_in_class(c, loc):
    codegen_nameop(c, loc, ".type_params", Load)
    codegen_nameop(c, loc, "__type_params__", Store)


def codegen_class_body(c, s, firstlineno):
    """Ultimately generate code for:
         <name> = __build_class__(<func>, <name>, *<bases>, **<keywords>)
    where:
      <func> is a zero arg function/closure created from the class body.
        It mutates its locals to build the class namespace.
      <name> is the class name
      <bases> is the positional arguments and *varargs argument
      <keywords> is the keyword arguments and **kwds argument
    This borrows from codegen_call.
    """
    # 1. compile the class body into a code object
    codegen_enter_scope(c, s.name, COMPILE_SCOPE_CLASS, id(s), firstlineno, s.name, None)
    loc = LOCATION(firstlineno, firstlineno, 0, 0)
    # load (global) __name__ ...
    codegen_nameop(c, loc, "__name__", Load)
    # ... and store it as __module__
    codegen_nameop(c, loc, "__module__", Store)
    addop_load_const(c, loc, c.u.u_metadata.u_qualname)
    codegen_nameop(c, loc, "__qualname__", Store)
    addop_load_const(c, loc, c.u.u_metadata.u_firstlineno)
    codegen_nameop(c, loc, "__firstlineno__", Store)
    if s.type_params:
        codegen_set_type_params_in_class(c, loc)
    if c.u.u_ste.ste_needs_classdict:
        addop(c, loc, LOAD_LOCALS)
        # We can't use codegen_nameop here because we need to generate a
        # STORE_DEREF in a class namespace, and codegen_nameop() won't do
        # that by default.
        addop_o(c, loc, STORE_DEREF, c.u.u_metadata.u_cellvars, "__classdict__")
    if c.u.u_ste.ste_has_conditional_annotations:
        addop_i(c, loc, BUILD_SET, 0)
        addop_o(c, loc, STORE_DEREF, c.u.u_metadata.u_cellvars, "__conditional_annotations__")
    # compile the body proper
    codegen_body(c, loc, s.body, False)
    addop_load_const(c, NO_LOCATION, c.static_attributes_as_tuple())
    codegen_nameop(c, NO_LOCATION, "__static_attributes__", Store)
    # The following code is artificial
    # Set __classdictcell__ if necessary
    if c.u.u_ste.ste_needs_classdict:
        # Store __classdictcell__ into class namespace
        i = c.lookup_cellvar("__classdict__")
        addop_i(c, NO_LOCATION, LOAD_CLOSURE, i)
        codegen_nameop(c, NO_LOCATION, "__classdictcell__", Store)
    # Return __classcell__ if it is referenced, otherwise return None
    if c.u.u_ste.ste_needs_class_closure:
        # Store __classcell__ into class namespace & return it
        i = c.lookup_cellvar("__class__")
        addop_i(c, NO_LOCATION, LOAD_CLOSURE, i)
        addop_i(c, NO_LOCATION, COPY, 1)
        codegen_nameop(c, NO_LOCATION, "__classcell__", Store)
    else:
        # No methods referenced __class__, so just return None
        addop_load_const(c, NO_LOCATION, None)
    addop(c, NO_LOCATION, RETURN_VALUE)
    # create the code object
    co = c.optimize_and_assemble(1)
    # leave the new scope
    c.exit_scope()

    # 2. load the 'build_class' function
    # these instructions should be attributed to the class line, not a
    # decorator line
    loc = LOC(s)
    addop(c, loc, LOAD_BUILD_CLASS)
    addop(c, loc, PUSH_NULL)
    # 3. load a function (or closure) made from the code object
    codegen_make_closure(c, loc, co, 0)
    # 4. load class name
    addop_load_const(c, loc, s.name)


def codegen_class(c, s):
    decos = s.decorator_list
    codegen_decorators(c, decos)
    firstlineno = s.lineno
    if decos:
        firstlineno = decos[0].lineno
    loc = LOC(s)
    type_params = s.type_params
    is_generic = len(type_params) > 0
    if is_generic:
        codegen_enter_scope(c, f"<generic parameters of {s.name}>", COMPILE_SCOPE_ANNOTATIONS,
                            id(type_params), firstlineno, s.name, None)
        codegen_type_params(c, type_params)
        codegen_nameop(c, loc, ".type_params", Store)

    codegen_class_body(c, s, firstlineno)

    # generate the rest of the code for the call
    if is_generic:
        codegen_nameop(c, loc, ".type_params", Load)
        addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_SUBSCRIPT_GENERIC)
        codegen_nameop(c, loc, ".generic_base", Store)
        codegen_call_helper_impl(c, loc, 2, s.bases, ".generic_base", s.keywords)
        co = c.optimize_and_assemble(0)
        c.exit_scope()
        codegen_make_closure(c, loc, co, 0)
        addop(c, loc, PUSH_NULL)
        addop_i(c, loc, CALL, 0)
    else:
        codegen_call_helper(c, loc, 2, s.bases, s.keywords)

    # 6. apply decorators
    codegen_apply_decorators(c, decos)
    # 7. store into <name>
    codegen_nameop(c, loc, s.name, Store)


def codegen_typealias_body(c, s):
    loc = LOC(s)
    name = s.name.id
    addop_load_const(c, loc, (1,))
    codegen_setup_annotations_scope(c, LOC(s), id(s), name)
    assert not c.u.u_ste.ste_has_docstring
    visit_expr(c, s.value)
    addop(c, loc, RETURN_VALUE)
    co = c.optimize_and_assemble(0)
    c.exit_scope()
    codegen_make_closure(c, loc, co, MAKE_FUNCTION_DEFAULTS)
    addop_i(c, loc, BUILD_TUPLE, 3)
    addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_TYPEALIAS)


def codegen_typealias(c, s):
    loc = LOC(s)
    type_params = s.type_params
    is_generic = len(type_params) > 0
    name = s.name.id
    if is_generic:
        codegen_enter_scope(c, f"<generic parameters of {name}>", COMPILE_SCOPE_ANNOTATIONS,
                            id(type_params), loc.lineno, None, None)
        addop_load_const(c, loc, name)
        codegen_type_params(c, type_params)
    else:
        addop_load_const(c, loc, name)
        addop_load_const(c, loc, None)

    codegen_typealias_body(c, s)

    if is_generic:
        co = c.optimize_and_assemble(0)
        c.exit_scope()
        codegen_make_closure(c, loc, co, 0)
        addop(c, loc, PUSH_NULL)
        addop_i(c, loc, CALL, 0)
    codegen_nameop(c, loc, name, Store)


def is_const_tuple(elts):
    return all(type(e) is _ast.Constant for e in elts)


def check_is_arg(e):
    """False if the expression is a constant value except named singletons."""
    if type(e) is _ast.Tuple:
        return not is_const_tuple(e.elts)
    if type(e) is not _ast.Constant:
        return True
    value = e.value
    return value is None or value is False or value is True or value is Ellipsis


def codegen_check_compare(c, e):
    """Check operands of identity checks ("is" and "is not").  Emit a warning
    if any operand is a constant except named singletons."""
    left = check_is_arg(e.left)
    left_expr = e.left
    for op, right_expr in zip(e.ops, e.comparators):
        right = check_is_arg(right_expr)
        if type(op) in (_ast.Is, _ast.IsNot):
            if not right or not left:
                literal = left_expr if not left else right_expr
                tp_name = infer_type(literal).__name__
                if type(op) is _ast.Is:
                    msg = f"\"is\" with '{tp_name:.200}' literal. Did you mean \"==\"?"
                else:
                    msg = f"\"is not\" with '{tp_name:.200}' literal. Did you mean \"!=\"?"
                c.warn(LOC(e), msg)
                return
        left = right
        left_expr = right_expr


_cmpops = {_ast.Eq: Py_EQ, _ast.NotEq: Py_NE, _ast.Lt: Py_LT, _ast.LtE: Py_LE,
           _ast.Gt: Py_GT, _ast.GtE: Py_GE}


def codegen_addcompare(c, loc, op):
    t = type(op)
    cmp = _cmpops.get(t)
    if cmp is None:
        if t is _ast.Is:
            addop_i(c, loc, IS_OP, 0)
        elif t is _ast.IsNot:
            addop_i(c, loc, IS_OP, 1)
        elif t is _ast.In:
            addop_i(c, loc, CONTAINS_OP, 0)
        elif t is _ast.NotIn:
            addop_i(c, loc, CONTAINS_OP, 1)
        else:
            raise AssertionError("unreachable")
        return
    # cmp goes in top three bits of the oparg, while the low four bits are
    # used by quickened versions of this opcode to store the comparison mask.
    # The fifth-lowest bit indicates whether the result should be converted
    # to bool and is set later):
    addop_i(c, loc, COMPARE_OP, (cmp << 5) | compare_masks[cmp])


def codegen_jump_if(c, loc, e, next, cond):
    t = type(e)
    if t is _ast.UnaryOp:
        if type(e.op) is _ast.Not:
            return codegen_jump_if(c, loc, e.operand, next, not cond)
        # fallback to general implementation
    elif t is _ast.BoolOp:
        s = e.values
        n = len(s) - 1
        assert n >= 0
        cond2 = type(e.op) is _ast.Or
        next2 = next
        if (not cond2) != (not cond):
            next2 = new_label(c)
        for i in range(n):
            codegen_jump_if(c, loc, s[i], next2, cond2)
        codegen_jump_if(c, loc, s[n], next, cond)
        if next2 != next:
            use_label(c, next2)
        return
    elif t is _ast.IfExp:
        end = new_label(c)
        next2 = new_label(c)
        codegen_jump_if(c, loc, e.test, next2, 0)
        codegen_jump_if(c, loc, e.body, next, cond)
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
        use_label(c, next2)
        codegen_jump_if(c, loc, e.orelse, next, cond)
        use_label(c, end)
        return
    elif t is _ast.Compare:
        n = len(e.ops) - 1
        if n > 0:
            codegen_check_compare(c, e)
            cleanup = new_label(c)
            visit_expr(c, e.left)
            for i in range(n):
                visit_expr(c, e.comparators[i])
                addop_i(c, LOC(e), SWAP, 2)
                addop_i(c, LOC(e), COPY, 2)
                codegen_addcompare(c, LOC(e), e.ops[i])
                addop(c, LOC(e), TO_BOOL)
                addop_j(c, LOC(e), POP_JUMP_IF_FALSE, cleanup)
            visit_expr(c, e.comparators[n])
            codegen_addcompare(c, LOC(e), e.ops[n])
            addop(c, LOC(e), TO_BOOL)
            addop_j(c, LOC(e), POP_JUMP_IF_TRUE if cond else POP_JUMP_IF_FALSE, next)
            end = new_label(c)
            addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
            use_label(c, cleanup)
            addop(c, LOC(e), POP_TOP)
            if not cond:
                addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, next)
            use_label(c, end)
            return
        # fallback to general implementation
    # general implementation
    visit_expr(c, e)
    addop(c, LOC(e), TO_BOOL)
    addop_j(c, LOC(e), POP_JUMP_IF_TRUE if cond else POP_JUMP_IF_FALSE, next)


def codegen_ifexp(c, e):
    end = new_label(c)
    next = new_label(c)
    codegen_jump_if(c, LOC(e), e.test, next, 0)
    visit_expr(c, e.body)
    addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
    use_label(c, next)
    visit_expr(c, e.orelse)
    use_label(c, end)


def codegen_lambda(c, e):
    args = e.args
    loc = LOC(e)
    funcflags = codegen_default_arguments(c, loc, args)
    umd = CodeUnitMetadata(len(args.args), len(args.posonlyargs), len(args.kwonlyargs))
    codegen_enter_scope(c, "<lambda>", COMPILE_SCOPE_LAMBDA, id(e), e.lineno, None, umd)
    assert not c.u.u_ste.ste_has_docstring
    visit_expr(c, e.body)
    if c.u.u_ste.ste_generator:
        co = c.optimize_and_assemble(0)
    else:
        addop(c, LOC(e.body), RETURN_VALUE)
        co = c.optimize_and_assemble(1)
    c.exit_scope()
    codegen_make_closure(c, loc, co, funcflags)


def codegen_if(c, s):
    end = new_label(c)
    if s.orelse:
        next = new_label(c)
    else:
        next = end
    codegen_jump_if(c, LOC(s), s.test, next, 0)
    visit_seq_stmt(c, s.body)
    if s.orelse:
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
        use_label(c, next)
        visit_seq_stmt(c, s.orelse)
    use_label(c, end)


def codegen_for(c, s):
    loc = LOC(s)
    start = new_label(c)
    body = new_label(c)
    cleanup = new_label(c)
    end = new_label(c)
    c.push_fblock(loc, COMPILE_FBLOCK_FOR_LOOP, start, end, None)
    visit_expr(c, s.iter)
    loc = LOC(s.iter)
    addop(c, loc, GET_ITER)
    use_label(c, start)
    addop_j(c, loc, FOR_ITER, cleanup)
    # Add NOP to ensure correct line tracing of multiline for statements.
    # It will be removed later if redundant.
    addop(c, LOC(s.target), NOP)
    use_label(c, body)
    visit_expr(c, s.target)
    visit_seq_stmt(c, s.body)
    # Mark jump as artificial
    addop_j(c, NO_LOCATION, JUMP, start)
    use_label(c, cleanup)
    # It is important for instrumentation that the `END_FOR` comes first.
    # Iteration over a generator will jump to the first of these
    # instructions, but a non-generator will jump to a later instruction.
    addop(c, NO_LOCATION, END_FOR)
    addop(c, NO_LOCATION, POP_ITER)
    c.pop_fblock(COMPILE_FBLOCK_FOR_LOOP, start)
    visit_seq_stmt(c, s.orelse)
    use_label(c, end)


def codegen_async_for(c, s):
    loc = LOC(s)
    start = new_label(c)
    send = new_label(c)
    except_ = new_label(c)
    end = new_label(c)
    visit_expr(c, s.iter)
    addop(c, LOC(s.iter), GET_AITER)
    use_label(c, start)
    c.push_fblock(loc, COMPILE_FBLOCK_FOR_LOOP, start, end, None)
    # SETUP_FINALLY to guard the __anext__ call
    addop_j(c, loc, SETUP_FINALLY, except_)
    addop(c, loc, GET_ANEXT)
    addop_load_const(c, loc, None)
    use_label(c, send)
    codegen_add_yield_from(c, loc, 1)
    addop(c, loc, POP_BLOCK)  # for SETUP_FINALLY
    addop(c, loc, NOT_TAKEN)
    # Success block for __anext__
    visit_expr(c, s.target)
    visit_seq_stmt(c, s.body)
    # Mark jump as artificial
    addop_j(c, NO_LOCATION, JUMP, start)
    c.pop_fblock(COMPILE_FBLOCK_FOR_LOOP, start)
    # Except block for __anext__
    use_label(c, except_)
    # Use same line number as the iterator, as the END_ASYNC_FOR succeeds the
    # `for`, not the body.
    loc = LOC(s.iter)
    addop_j(c, loc, END_ASYNC_FOR, send)
    # `else` block
    visit_seq_stmt(c, s.orelse)
    use_label(c, end)


def codegen_while(c, s):
    loop = new_label(c)
    end = new_label(c)
    anchor = new_label(c)
    use_label(c, loop)
    c.push_fblock(LOC(s), COMPILE_FBLOCK_WHILE_LOOP, loop, end, None)
    codegen_jump_if(c, LOC(s), s.test, anchor, 0)
    visit_seq_stmt(c, s.body)
    addop_j(c, NO_LOCATION, JUMP, loop)
    c.pop_fblock(COMPILE_FBLOCK_WHILE_LOOP, loop)
    use_label(c, anchor)
    if s.orelse:
        visit_seq_stmt(c, s.orelse)
    use_label(c, end)


def codegen_return(c, s):
    loc = LOC(s)
    value = s.value
    preserve_tos = value is not None and type(value) is not _ast.Constant
    ste = c.u.u_ste
    if not symtable.is_function_like(ste):
        c.error(loc, "'return' outside function")
    if value is not None and ste.ste_coroutine and ste.ste_generator:
        c.error(loc, "'return' with value in async generator")
    if preserve_tos:
        visit_expr(c, value)
    else:
        # Emit instruction with line number for return value
        if value is not None:
            loc = LOC(value)
            addop(c, loc, NOP)
    if value is None or value.lineno != s.lineno:
        loc = LOC(s)
        addop(c, loc, NOP)
    loc, _ = codegen_unwind_fblock_stack(c, loc, preserve_tos, False)
    if value is None:
        addop_load_const(c, loc, None)
    elif not preserve_tos:
        addop_load_const(c, loc, value.value)
    addop(c, loc, RETURN_VALUE)


def codegen_break(c, loc):
    origin_loc = loc
    # Emit instruction with line number
    addop(c, loc, NOP)
    loc, loop = codegen_unwind_fblock_stack(c, loc, 0, True)
    if loop is None:
        c.error(origin_loc, "'break' outside loop")
    loc = codegen_unwind_fblock(c, loc, loop, 0)
    addop_j(c, loc, JUMP, loop.fb_exit)


def codegen_continue(c, loc):
    origin_loc = loc
    # Emit instruction with line number
    addop(c, loc, NOP)
    loc, loop = codegen_unwind_fblock_stack(c, loc, 0, True)
    if loop is None:
        c.error(origin_loc, "'continue' not properly in loop")
    addop_j(c, loc, JUMP, loop.fb_block)


# Code generated for "try: <body> finally: <finalbody>" is as follows:
#
#         SETUP_FINALLY           L
#         <code for body>
#         POP_BLOCK
#         <code for finalbody>
#         JUMP E
#     L:
#         <code for finalbody>
#     E:
#
# The special instructions use the block stack.  Each block stack entry
# contains the instruction that created it (here SETUP_FINALLY), the level of
# the value stack at the time the block stack entry was created, and a label
# (here L).
#
# SETUP_FINALLY:
#   Pushes the current value stack level and the label onto the block stack.
# POP_BLOCK:
#   Pops en entry from the block stack.
#
# The block stack is unwound when an exception is raised: when a
# SETUP_FINALLY entry is found, the raised and the caught exceptions are
# pushed onto the value stack (and the exception condition is cleared), and
# the interpreter jumps to the label gotten from the block stack.

def codegen_try_finally(c, s, star=False):
    """codegen_try_finally, and codegen_try_star_finally when star."""
    loc = LOC(s)
    body = new_label(c)
    end = new_label(c)
    exit = new_label(c)
    cleanup = new_label(c)
    # `try` block
    addop_j(c, loc, SETUP_FINALLY, end)
    use_label(c, body)
    c.push_fblock(loc, COMPILE_FBLOCK_FINALLY_TRY, body, end, s.finalbody)
    if s.handlers:
        (codegen_try_star_except if star else codegen_try_except)(c, s)
    else:
        visit_seq_stmt(c, s.body)
    addop(c, NO_LOCATION, POP_BLOCK)
    c.pop_fblock(COMPILE_FBLOCK_FINALLY_TRY, body)
    visit_seq_stmt(c, s.finalbody)
    addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, exit)
    # `finally` block
    use_label(c, end)
    loc = NO_LOCATION
    addop_j(c, loc, SETUP_CLEANUP, cleanup)
    addop(c, loc, PUSH_EXC_INFO)
    c.push_fblock(loc, COMPILE_FBLOCK_FINALLY_END, end, NO_LABEL, None)
    visit_seq_stmt(c, s.finalbody)
    c.pop_fblock(COMPILE_FBLOCK_FINALLY_END, end)
    loc = NO_LOCATION
    addop_i(c, loc, RERAISE, 0)
    use_label(c, cleanup)
    codegen_pop_except_and_reraise(c, loc)
    use_label(c, exit)


# Code generated for "try: S except E1 as V1: S1 except E2 as V2: S2 ...":
# (The contents of the value stack is shown in [], with the top at the right;
# 'tb' is trace-back info, 'val' the exception's associated value, and 'exc'
# the exception.)
#
# Value stack          Label   Instruction     Argument
# []                           SETUP_FINALLY   L1
# []                           <code for S>
# []                           POP_BLOCK
# []                           JUMP            L0
#
# [exc]                L1:     <evaluate E1>           )
# [exc, E1]                    CHECK_EXC_MATCH         )
# [exc, bool]                  POP_JUMP_IF_FALSE L2    ) only if E1
# [exc]                        <assign to V1>  (or POP if no V1)
# []                           <code for S1>
#                              JUMP            L0
#
# [exc]                L2:     <evaluate E2>
# .............................etc.......................
#
# [exc]                Ln+1:   RERAISE     # re-raise exception
#
# []                   L0:     <next statement>
#
# Of course, parts are not generated if Vi or Ei is not present.

def codegen_try_except(c, s):
    loc = LOC(s)
    body = new_label(c)
    except_ = new_label(c)
    end = new_label(c)
    cleanup = new_label(c)

    addop_j(c, loc, SETUP_FINALLY, except_)
    use_label(c, body)
    c.push_fblock(loc, COMPILE_FBLOCK_TRY_EXCEPT, body, NO_LABEL, None)
    visit_seq_stmt(c, s.body)
    c.pop_fblock(COMPILE_FBLOCK_TRY_EXCEPT, body)
    addop(c, NO_LOCATION, POP_BLOCK)
    if s.orelse:
        visit_seq_stmt(c, s.orelse)
    addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
    n = len(s.handlers)

    use_label(c, except_)
    addop_j(c, NO_LOCATION, SETUP_CLEANUP, cleanup)
    addop(c, NO_LOCATION, PUSH_EXC_INFO)
    # Runtime will push a block here, so we need to account for that
    c.push_fblock(loc, COMPILE_FBLOCK_EXCEPTION_HANDLER, NO_LABEL, NO_LABEL, None)

    for i, handler in enumerate(s.handlers):
        loc = LOC(handler)
        if not handler.type and i < n - 1:
            c.error(loc, "default 'except:' must be last")
        except_ = new_label(c)
        if handler.type:
            visit_expr(c, handler.type)
            addop(c, loc, CHECK_EXC_MATCH)
            addop_j(c, loc, POP_JUMP_IF_FALSE, except_)
        if handler.name:
            cleanup_end = new_label(c)
            cleanup_body = new_label(c)
            codegen_nameop(c, loc, handler.name, Store)
            # try:
            #     # body
            # except type as name:
            #     try:
            #         # body
            #     finally:
            #         name = None # in case body contains "del name"
            #         del name

            # second try:
            addop_j(c, loc, SETUP_CLEANUP, cleanup_end)
            use_label(c, cleanup_body)
            c.push_fblock(loc, COMPILE_FBLOCK_HANDLER_CLEANUP, cleanup_body, NO_LABEL,
                          handler.name)
            # second # body
            visit_seq_stmt(c, handler.body)
            c.pop_fblock(COMPILE_FBLOCK_HANDLER_CLEANUP, cleanup_body)
            # name = None; del name; # Mark as artificial
            addop(c, NO_LOCATION, POP_BLOCK)
            addop(c, NO_LOCATION, POP_BLOCK)
            addop(c, NO_LOCATION, POP_EXCEPT)
            addop_load_const(c, NO_LOCATION, None)
            codegen_nameop(c, NO_LOCATION, handler.name, Store)
            codegen_nameop(c, NO_LOCATION, handler.name, Del)
            addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
            # except:
            use_label(c, cleanup_end)
            # name = None; del name; # artificial
            addop_load_const(c, NO_LOCATION, None)
            codegen_nameop(c, NO_LOCATION, handler.name, Store)
            codegen_nameop(c, NO_LOCATION, handler.name, Del)
            addop_i(c, NO_LOCATION, RERAISE, 1)
        else:
            cleanup_body = new_label(c)
            addop(c, loc, POP_TOP)  # exc_value
            use_label(c, cleanup_body)
            c.push_fblock(loc, COMPILE_FBLOCK_HANDLER_CLEANUP, cleanup_body, NO_LABEL, None)
            visit_seq_stmt(c, handler.body)
            c.pop_fblock(COMPILE_FBLOCK_HANDLER_CLEANUP, cleanup_body)
            addop(c, NO_LOCATION, POP_BLOCK)
            addop(c, NO_LOCATION, POP_EXCEPT)
            addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
        use_label(c, except_)
    # artificial
    c.pop_fblock(COMPILE_FBLOCK_EXCEPTION_HANDLER, NO_LABEL)
    addop_i(c, NO_LOCATION, RERAISE, 0)
    use_label(c, cleanup)
    codegen_pop_except_and_reraise(c, NO_LOCATION)
    use_label(c, end)


# Code generated for "try: S except* E1 as V1: S1 except* E2 as V2: S2 ...":
# (The contents of the value stack is shown in [], with the top at the right;
# 'tb' is trace-back info, 'val' the exception instance, and 'typ' the
# exception's type.)
#
# Value stack                   Label         Instruction     Argument
# []                                         SETUP_FINALLY         L1
# []                                         <code for S>
# []                                         POP_BLOCK
# []                                         JUMP                  L0
#
# [exc]                            L1:       BUILD_LIST   )  list for raised/reraised excs ("result")
# [orig, res]                                COPY 2       )  make a copy of the original EG
#
# [orig, res, exc]                           <evaluate E1>
# [orig, res, exc, E1]                       CHECK_EG_MATCH
# [orig, res, rest/exc, match?]              COPY 1
# [orig, res, rest/exc, match?, match?]      POP_JUMP_IF_NONE      C1
#
# [orig, res, rest, match]                   <assign to V1>  (or POP if no V1)
#
# [orig, res, rest]                          SETUP_FINALLY         R1
# [orig, res, rest]                          <code for S1>
# [orig, res, rest]                          JUMP                  L2
#
# [orig, res, rest, i, v]          R1:       LIST_APPEND   3 ) exc raised in except* body - add to res
# [orig, res, rest, i]                       POP
# [orig, res, rest]                          JUMP                  LE2
#
# [orig, res, rest]                L2:       NOP  ) for lineno
# [orig, res, rest]                          JUMP                  LE2
#
# [orig, res, rest/exc, None]      C1:       POP
#
# [orig, res, rest]               LE2:       <evaluate E2>
# .............................etc.......................
#
# [orig, res, rest]                Ln+1:     LIST_APPEND 1  ) add unhandled exc to res (could be None)
#
# [orig, res]                                CALL_INTRINSIC_2 PREP_RERAISE_STAR
# [exc]                                      COPY 1
# [exc, exc]                                 POP_JUMP_IF_NOT_NONE  RER
# [exc]                                      POP_TOP
# []                                         JUMP                  L0
#
# [exc]                            RER:      SWAP 2
# [exc, prev_exc_info]                       POP_EXCEPT
# [exc]                                      RERAISE               0
#
# []                               L0:       <next statement>

def codegen_try_star_except(c, s):
    loc = LOC(s)
    body = new_label(c)
    except_ = new_label(c)
    orelse = new_label(c)
    end = new_label(c)
    cleanup = new_label(c)
    reraise_star = new_label(c)

    addop_j(c, loc, SETUP_FINALLY, except_)
    use_label(c, body)
    c.push_fblock(loc, COMPILE_FBLOCK_TRY_EXCEPT, body, NO_LABEL, None)
    visit_seq_stmt(c, s.body)
    c.pop_fblock(COMPILE_FBLOCK_TRY_EXCEPT, body)
    addop(c, NO_LOCATION, POP_BLOCK)
    addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, orelse)
    n = len(s.handlers)

    use_label(c, except_)
    addop_j(c, NO_LOCATION, SETUP_CLEANUP, cleanup)
    addop(c, NO_LOCATION, PUSH_EXC_INFO)
    # Runtime will push a block here, so we need to account for that
    c.push_fblock(loc, COMPILE_FBLOCK_EXCEPTION_GROUP_HANDLER, NO_LABEL, NO_LABEL,
                  "except handler")

    for i, handler in enumerate(s.handlers):
        loc = LOC(handler)
        except_ = new_label(c)
        except_with_error = new_label(c)
        no_match = new_label(c)
        if i == 0:
            # create empty list for exceptions raised/reraise in the except* blocks
            #    [orig]       BUILD_LIST
            # Create a copy of the original EG
            #    [orig, []]   COPY 2
            #    [orig, [], exc]
            addop_i(c, loc, BUILD_LIST, 0)
            addop_i(c, loc, COPY, 2)
        if handler.type:
            visit_expr(c, handler.type)
            addop(c, loc, CHECK_EG_MATCH)
            addop_i(c, loc, COPY, 1)
            addop_j(c, loc, POP_JUMP_IF_NONE, no_match)

        cleanup_end = new_label(c)
        cleanup_body = new_label(c)
        if handler.name:
            codegen_nameop(c, loc, handler.name, Store)
        else:
            addop(c, loc, POP_TOP)  # match
        # try:
        #     # body
        # except type as name:
        #     try:
        #         # body
        #     finally:
        #         name = None # in case body contains "del name"
        #         del name

        # second try:
        addop_j(c, loc, SETUP_CLEANUP, cleanup_end)
        use_label(c, cleanup_body)
        c.push_fblock(loc, COMPILE_FBLOCK_HANDLER_CLEANUP, cleanup_body, NO_LABEL, handler.name)
        # second # body
        visit_seq_stmt(c, handler.body)
        c.pop_fblock(COMPILE_FBLOCK_HANDLER_CLEANUP, cleanup_body)
        # name = None; del name; # artificial
        addop(c, NO_LOCATION, POP_BLOCK)
        if handler.name:
            addop_load_const(c, NO_LOCATION, None)
            codegen_nameop(c, NO_LOCATION, handler.name, Store)
            codegen_nameop(c, NO_LOCATION, handler.name, Del)
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, except_)
        # except:
        use_label(c, cleanup_end)
        # name = None; del name; # artificial
        if handler.name:
            addop_load_const(c, NO_LOCATION, None)
            codegen_nameop(c, NO_LOCATION, handler.name, Store)
            codegen_nameop(c, NO_LOCATION, handler.name, Del)
        # add exception raised to the res list
        addop_i(c, NO_LOCATION, LIST_APPEND, 3)  # exc
        addop(c, NO_LOCATION, POP_TOP)  # lasti
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, except_with_error)

        use_label(c, except_)
        addop(c, NO_LOCATION, NOP)  # to hold a propagated location info
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, except_with_error)

        use_label(c, no_match)
        addop(c, loc, POP_TOP)  # match (None)

        use_label(c, except_with_error)
        if i == n - 1:
            # Add exc to the list (if not None it's the unhandled part of the EG)
            addop_i(c, NO_LOCATION, LIST_APPEND, 1)
            addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, reraise_star)
    # artificial
    c.pop_fblock(COMPILE_FBLOCK_EXCEPTION_GROUP_HANDLER, NO_LABEL)
    reraise = new_label(c)

    use_label(c, reraise_star)
    addop_i(c, NO_LOCATION, CALL_INTRINSIC_2, INTRINSIC_PREP_RERAISE_STAR)
    addop_i(c, NO_LOCATION, COPY, 1)
    addop_j(c, NO_LOCATION, POP_JUMP_IF_NOT_NONE, reraise)
    # Nothing to reraise
    addop(c, NO_LOCATION, POP_TOP)
    addop(c, NO_LOCATION, POP_BLOCK)
    addop(c, NO_LOCATION, POP_EXCEPT)
    addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)

    use_label(c, reraise)
    addop(c, NO_LOCATION, POP_BLOCK)
    addop_i(c, NO_LOCATION, SWAP, 2)
    addop(c, NO_LOCATION, POP_EXCEPT)
    addop_i(c, NO_LOCATION, RERAISE, 0)

    use_label(c, cleanup)
    codegen_pop_except_and_reraise(c, NO_LOCATION)

    use_label(c, orelse)
    visit_seq_stmt(c, s.orelse)

    use_label(c, end)


def codegen_try(c, s):
    if s.finalbody:
        codegen_try_finally(c, s)
    else:
        codegen_try_except(c, s)


def codegen_try_star(c, s):
    if s.finalbody:
        codegen_try_finally(c, s, star=True)
    else:
        codegen_try_star_except(c, s)


def codegen_import_as(c, loc, name, asname):
    """The IMPORT_NAME opcode was already generated.  This function merely
    needs to bind the result to a name.  If there is a dot in name, we need
    to split it and emit a IMPORT_FROM for each name."""
    if "." in name:
        # Consume the base module name to get the first attribute
        attrs = name.split(".")[1:]
        for i, attr in enumerate(attrs):
            addop_o(c, loc, IMPORT_FROM, names(c), attr)
            if i == len(attrs) - 1:
                break
            addop_i(c, loc, SWAP, 2)
            addop(c, loc, POP_TOP)
        codegen_nameop(c, loc, asname, Store)
        addop(c, loc, POP_TOP)
        return
    codegen_nameop(c, loc, asname, Store)


def codegen_import(c, s):
    """The Import node stores a module name like a.b.c as a single string.
    This is convenient for all cases except
      import a.b.c as d
    where we need to parse that string to extract the individual module names.
    """
    loc = LOC(s)
    for alias in s.names:
        addop_load_const(c, loc, 0)
        addop_load_const(c, loc, None)
        addop_name(c, loc, IMPORT_NAME, names(c), alias.name)
        if alias.asname:
            codegen_import_as(c, loc, alias.name, alias.asname)
        else:
            tmp = alias.name
            dot = tmp.find(".")
            if dot != -1:
                tmp = tmp[:dot]
            codegen_nameop(c, loc, tmp, Store)


def codegen_from_import(c, s):
    addop_load_const(c, LOC(s), s.level)
    # build up the names
    addop_load_const(c, LOC(s), tuple(alias.name for alias in s.names))
    addop_name(c, LOC(s), IMPORT_NAME, names(c), s.module if s.module else "")
    for i, alias in enumerate(s.names):
        if i == 0 and alias.name[0] == "*":
            assert len(s.names) == 1
            addop_i(c, LOC(s), CALL_INTRINSIC_1, INTRINSIC_IMPORT_STAR)
            addop(c, NO_LOCATION, POP_TOP)
            return
        addop_name(c, LOC(s), IMPORT_FROM, names(c), alias.name)
        store_name = alias.asname if alias.asname else alias.name
        codegen_nameop(c, LOC(s), store_name, Store)
    # remove imported module
    addop(c, LOC(s), POP_TOP)


def codegen_assert(c, s):
    test = s.test
    # Always emit a warning if the test is a non-zero length tuple
    if ((type(test) is _ast.Tuple and len(test.elts) > 0)
            or (type(test) is _ast.Constant and isinstance(test.value, tuple)
                and len(test.value) > 0)):
        c.warn(LOC(s), "assertion is always true, perhaps remove parentheses?")
    if c.c_optimize:
        return
    end = new_label(c)
    codegen_jump_if(c, LOC(s), test, end, 1)
    addop_i(c, LOC(s), LOAD_COMMON_CONSTANT, CONSTANT_ASSERTIONERROR)
    if s.msg:
        visit_expr(c, s.msg)
        addop_i(c, LOC(s), CALL, 0)
    addop_i(c, LOC(test), RAISE_VARARGS, 1)
    use_label(c, end)


def codegen_stmt_expr(c, loc, value):
    if c.is_interactive_top_level():
        visit_expr(c, value)
        addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_PRINT)
        addop(c, NO_LOCATION, POP_TOP)
        return
    if type(value) is _ast.Constant:
        # ignore constant statement
        addop(c, loc, NOP)
        return
    visit_expr(c, value)
    addop(c, NO_LOCATION, POP_TOP)  # artificial


def codegen_cond_block(func, c, s):
    c.enter_conditional_block()
    func(c, s)
    c.leave_conditional_block()


def codegen_delete(c, s):
    visit_seq_expr(c, s.targets)


def codegen_assign(c, s):
    n = len(s.targets)
    visit_expr(c, s.value)
    for i, target in enumerate(s.targets):
        if i < n - 1:
            addop_i(c, LOC(s), COPY, 1)
        visit_expr(c, target)


def codegen_raise(c, s):
    n = 0
    if s.exc:
        visit_expr(c, s.exc)
        n += 1
        if s.cause:
            visit_expr(c, s.cause)
            n += 1
    addop_i(c, LOC(s), RAISE_VARARGS, n)


def codegen_pass(c, s):
    addop(c, LOC(s), NOP)


def codegen_nothing(c, s):
    pass


_stmt_visitors = {
    _ast.FunctionDef: lambda c, s: codegen_function(c, s, 0),
    _ast.ClassDef: codegen_class,
    _ast.TypeAlias: codegen_typealias,
    _ast.Return: codegen_return,
    _ast.Delete: codegen_delete,
    _ast.Assign: codegen_assign,
    _ast.AugAssign: lambda c, s: codegen_augassign(c, s),
    _ast.AnnAssign: lambda c, s: codegen_annassign(c, s),
    _ast.For: lambda c, s: codegen_cond_block(codegen_for, c, s),
    _ast.While: lambda c, s: codegen_cond_block(codegen_while, c, s),
    _ast.If: lambda c, s: codegen_cond_block(codegen_if, c, s),
    _ast.Match: lambda c, s: codegen_cond_block(codegen_match, c, s),
    _ast.Raise: codegen_raise,
    _ast.Try: lambda c, s: codegen_cond_block(codegen_try, c, s),
    _ast.TryStar: lambda c, s: codegen_cond_block(codegen_try_star, c, s),
    _ast.Assert: codegen_assert,
    _ast.Import: codegen_import,
    _ast.ImportFrom: codegen_from_import,
    _ast.Global: codegen_nothing,
    _ast.Nonlocal: codegen_nothing,
    _ast.Expr: lambda c, s: codegen_stmt_expr(c, LOC(s), s.value),
    _ast.Pass: codegen_pass,
    _ast.Break: lambda c, s: codegen_break(c, LOC(s)),
    _ast.Continue: lambda c, s: codegen_continue(c, LOC(s)),
    _ast.With: lambda c, s: codegen_cond_block(codegen_with, c, s),
    _ast.AsyncFunctionDef: lambda c, s: codegen_function(c, s, 1),
    _ast.AsyncWith: lambda c, s: codegen_cond_block(codegen_async_with, c, s),
    _ast.AsyncFor: lambda c, s: codegen_cond_block(codegen_async_for, c, s),
}


def visit_stmt(c, s):
    """codegen_visit_stmt."""
    _stmt_visitors[type(s)](c, s)


def unaryop(op):
    t = type(op)
    if t is _ast.Invert:
        return UNARY_INVERT
    if t is _ast.USub:
        return UNARY_NEGATIVE
    raise SystemError(f"unary op {t.__name__} should not be possible")


_binops = {
    _ast.Add: (NB_ADD, NB_INPLACE_ADD),
    _ast.Sub: (NB_SUBTRACT, NB_INPLACE_SUBTRACT),
    _ast.Mult: (NB_MULTIPLY, NB_INPLACE_MULTIPLY),
    _ast.MatMult: (NB_MATRIX_MULTIPLY, NB_INPLACE_MATRIX_MULTIPLY),
    _ast.Div: (NB_TRUE_DIVIDE, NB_INPLACE_TRUE_DIVIDE),
    _ast.Mod: (NB_REMAINDER, NB_INPLACE_REMAINDER),
    _ast.Pow: (NB_POWER, NB_INPLACE_POWER),
    _ast.LShift: (NB_LSHIFT, NB_INPLACE_LSHIFT),
    _ast.RShift: (NB_RSHIFT, NB_INPLACE_RSHIFT),
    _ast.BitOr: (NB_OR, NB_INPLACE_OR),
    _ast.BitXor: (NB_XOR, NB_INPLACE_XOR),
    _ast.BitAnd: (NB_AND, NB_INPLACE_AND),
    _ast.FloorDiv: (NB_FLOOR_DIVIDE, NB_INPLACE_FLOOR_DIVIDE),
}


def addop_binary(c, loc, binop, inplace):
    oparg = _binops[type(binop)][inplace]
    addop_i(c, loc, BINARY_OP, oparg)


def codegen_addop_yield(c, loc):
    ste = c.u.u_ste
    if ste.ste_generator and ste.ste_coroutine:
        addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_ASYNC_GEN_WRAP)
    addop_i(c, loc, YIELD_VALUE, 0)
    addop_i(c, loc, RESUME, RESUME_AFTER_YIELD)


def codegen_load_classdict_freevar(c, loc):
    addop_o(c, loc, LOAD_DEREF, c.u.u_metadata.u_freevars, "__classdict__")


def codegen_nameop(c, loc, name, ctx):
    """Load, store or delete name; ctx is Load, Store or Del."""
    assert name not in ("None", "True", "False")
    mangled = c.maybe_mangle(name)
    scope = symtable.get_scope(c.u.u_ste, mangled)
    optype, arg = c.resolve_nameop(mangled, scope)
    # XXX Leave assert here, but handle __doc__ and the like better
    assert scope or name[0] == "_"
    ste = c.u.u_ste
    if optype == COMPILE_OP_DEREF:
        if ctx is Load:
            if ste.ste_type == symtable.ClassBlock and not c.u.u_in_inlined_comp:
                op = LOAD_FROM_DICT_OR_DEREF
                # First load the locals
                addop(c, loc, LOAD_LOCALS)
            elif ste.ste_can_see_class_scope:
                op = LOAD_FROM_DICT_OR_DEREF
                # First load the classdict
                codegen_load_classdict_freevar(c, loc)
            else:
                op = LOAD_DEREF
        elif ctx is Store:
            op = STORE_DEREF
        else:
            op = DELETE_DEREF
    elif optype == COMPILE_OP_FAST:
        op = LOAD_FAST if ctx is Load else STORE_FAST if ctx is Store else DELETE_FAST
        addop_o(c, loc, op, c.u.u_metadata.u_varnames, mangled)
        return
    elif optype == COMPILE_OP_GLOBAL:
        if ctx is Load:
            if ste.ste_can_see_class_scope and scope == symtable.GLOBAL_IMPLICIT:
                op = LOAD_FROM_DICT_OR_GLOBALS
                # First load the classdict
                codegen_load_classdict_freevar(c, loc)
            else:
                op = LOAD_GLOBAL
        elif ctx is Store:
            op = STORE_GLOBAL
        else:
            op = DELETE_GLOBAL
    else:
        assert optype == COMPILE_OP_NAME
        if ctx is Load:
            op = (LOAD_GLOBAL if ste.ste_type == symtable.ClassBlock and c.u.u_in_inlined_comp
                  else LOAD_NAME)
        elif ctx is Store:
            op = STORE_NAME
        else:
            op = DELETE_NAME
    if op == LOAD_GLOBAL:
        arg <<= 1
    addop_i(c, loc, op, arg)


def codegen_boolop(c, e):
    loc = LOC(e)
    jumpi = JUMP_IF_FALSE if type(e.op) is _ast.And else JUMP_IF_TRUE
    end = new_label(c)
    s = e.values
    n = len(s) - 1
    assert n >= 0
    for i in range(n):
        visit_expr(c, s[i])
        addop_j(c, loc, jumpi, end)
        addop(c, loc, POP_TOP)
    visit_expr(c, s[n])
    use_label(c, end)


def starunpack_helper(c, loc, elts, pushed, build, add, extend, tuple_, injected_arg=None):
    """starunpack_helper_impl, and starunpack_helper without injected_arg."""
    n = len(elts)
    big = n + pushed + (1 if injected_arg else 0) > STACK_USE_GUIDELINE
    seen_star = any(type(elt) is _ast.Starred for elt in elts)
    if not seen_star and not big:
        for elt in elts:
            visit_expr(c, elt)
        if injected_arg:
            codegen_nameop(c, loc, injected_arg, Load)
            n += 1
        if tuple_:
            addop_i(c, loc, BUILD_TUPLE, n + pushed)
        else:
            addop_i(c, loc, build, n + pushed)
        return
    sequence_built = False
    if big:
        addop_i(c, loc, build, pushed)
        sequence_built = True
    for i, elt in enumerate(elts):
        if type(elt) is _ast.Starred:
            if not sequence_built:
                addop_i(c, loc, build, i + pushed)
                sequence_built = True
            visit_expr(c, elt.value)
            addop_i(c, loc, extend, 1)
        else:
            visit_expr(c, elt)
            if sequence_built:
                addop_i(c, loc, add, 1)
    assert sequence_built
    if injected_arg:
        codegen_nameop(c, loc, injected_arg, Load)
        addop_i(c, loc, add, 1)
    if tuple_:
        addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_LIST_TO_TUPLE)


def unpack_helper(c, loc, elts):
    n = len(elts)
    seen_star = False
    for i, elt in enumerate(elts):
        if type(elt) is _ast.Starred and not seen_star:
            if i >= (1 << 8) or n - i - 1 >= (0x7FFFFFFF >> 8):
                c.error(loc, "too many expressions in star-unpacking assignment")
            addop_i(c, loc, UNPACK_EX, i + ((n - i - 1) << 8))
            seen_star = True
        elif type(elt) is _ast.Starred:
            c.error(loc, "multiple starred expressions in assignment")
    if not seen_star:
        addop_i(c, loc, UNPACK_SEQUENCE, n)


def assignment_helper(c, loc, elts):
    unpack_helper(c, loc, elts)
    for elt in elts:
        visit_expr(c, elt.value if type(elt) is _ast.Starred else elt)


def codegen_list(c, e):
    loc = LOC(e)
    ctx = type(e.ctx)
    if ctx is Store:
        assignment_helper(c, loc, e.elts)
    elif ctx is Load:
        starunpack_helper(c, loc, e.elts, 0, BUILD_LIST, LIST_APPEND, LIST_EXTEND, 0)
    else:
        visit_seq_expr(c, e.elts)


def codegen_tuple(c, e):
    loc = LOC(e)
    ctx = type(e.ctx)
    if ctx is Store:
        assignment_helper(c, loc, e.elts)
    elif ctx is Load:
        starunpack_helper(c, loc, e.elts, 0, BUILD_LIST, LIST_APPEND, LIST_EXTEND, 1)
    else:
        visit_seq_expr(c, e.elts)


def codegen_set(c, e):
    starunpack_helper(c, LOC(e), e.elts, 0, BUILD_SET, SET_ADD, SET_UPDATE, 0)


def codegen_subdict(c, e, begin, end):
    n = end - begin
    big = n * 2 > STACK_USE_GUIDELINE
    loc = LOC(e)
    if big:
        addop_i(c, loc, BUILD_MAP, 0)
    for i in range(begin, end):
        visit_expr(c, e.keys[i])
        visit_expr(c, e.values[i])
        if big:
            addop_i(c, loc, MAP_ADD, 1)
    if not big:
        addop_i(c, loc, BUILD_MAP, n)


def codegen_dict(c, e):
    loc = LOC(e)
    n = len(e.values)
    have_dict = False
    elements = 0
    for i in range(n):
        is_unpacking = e.keys[i] is None
        if is_unpacking:
            if elements:
                codegen_subdict(c, e, i - elements, i)
                if have_dict:
                    addop_i(c, loc, DICT_UPDATE, 1)
                have_dict = True
                elements = 0
            if not have_dict:
                addop_i(c, loc, BUILD_MAP, 0)
                have_dict = True
            visit_expr(c, e.values[i])
            addop_i(c, loc, DICT_UPDATE, 1)
        else:
            if elements * 2 > STACK_USE_GUIDELINE:
                codegen_subdict(c, e, i - elements, i + 1)
                if have_dict:
                    addop_i(c, loc, DICT_UPDATE, 1)
                have_dict = True
                elements = 0
            else:
                elements += 1
    if elements:
        codegen_subdict(c, e, n - elements, n)
        if have_dict:
            addop_i(c, loc, DICT_UPDATE, 1)
        have_dict = True
    if not have_dict:
        addop_i(c, loc, BUILD_MAP, 0)


def codegen_compare(c, e):
    loc = LOC(e)
    codegen_check_compare(c, e)
    visit_expr(c, e.left)
    assert len(e.ops) > 0
    n = len(e.ops) - 1
    if n == 0:
        visit_expr(c, e.comparators[0])
        codegen_addcompare(c, loc, e.ops[0])
    else:
        cleanup = new_label(c)
        for i in range(n):
            visit_expr(c, e.comparators[i])
            addop_i(c, loc, SWAP, 2)
            addop_i(c, loc, COPY, 2)
            codegen_addcompare(c, loc, e.ops[i])
            addop_i(c, loc, COPY, 1)
            addop(c, loc, TO_BOOL)
            addop_j(c, loc, POP_JUMP_IF_FALSE, cleanup)
            addop(c, loc, POP_TOP)
        visit_expr(c, e.comparators[n])
        codegen_addcompare(c, loc, e.ops[n])
        end = new_label(c)
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
        use_label(c, cleanup)
        addop_i(c, loc, SWAP, 2)
        addop(c, loc, POP_TOP)
        use_label(c, end)


def _template_type():
    from string.templatelib import Template
    return Template


_inferred_types = {
    _ast.Tuple: tuple, _ast.List: list, _ast.ListComp: list, _ast.Dict: dict,
    _ast.DictComp: dict, _ast.Set: set, _ast.SetComp: set,
    _ast.GeneratorExp: type(_ for _ in ()), _ast.Lambda: type(lambda: None),
    _ast.JoinedStr: str, _ast.FormattedValue: str,
}


def infer_type(e):
    t = type(e)
    if t is _ast.Constant:
        return type(e.value)
    if t is _ast.TemplateStr or t is _ast.Interpolation:
        return _template_type()
    return _inferred_types.get(t)


def tp_name(t):
    """The type's C tp_name."""
    return t.__name__ if t.__module__ == "builtins" else f"{t.__module__}.{t.__qualname__}"


_uncallable = (_ast.Constant, _ast.Tuple, _ast.List, _ast.ListComp, _ast.Dict, _ast.DictComp,
               _ast.Set, _ast.SetComp, _ast.GeneratorExp, _ast.JoinedStr, _ast.TemplateStr,
               _ast.FormattedValue, _ast.Interpolation)


def check_caller(c, e):
    if type(e) in _uncallable:
        c.warn(LOC(e), f"'{tp_name(infer_type(e)):.200}' object is not callable; "
                       "perhaps you missed a comma?")


def check_subscripter(c, e):
    t = type(e)
    if t is _ast.Constant:
        v = e.value
        if not (v is None or v is Ellipsis or isinstance(v, (int, float, complex, set, frozenset))):
            return
    elif t not in (_ast.Set, _ast.SetComp, _ast.GeneratorExp, _ast.TemplateStr,
                   _ast.Interpolation, _ast.Lambda):
        return
    c.warn(LOC(e), f"'{tp_name(infer_type(e)):.200}' object is not subscriptable; "
                   "perhaps you missed a comma?")


def check_index(c, e, s):
    index_type = infer_type(s)
    if index_type is None or issubclass(index_type, int) or index_type is slice:
        return
    t = type(e)
    if t is _ast.Constant:
        if not isinstance(e.value, (str, bytes, tuple)):
            return
    elif t not in (_ast.Tuple, _ast.List, _ast.ListComp, _ast.JoinedStr, _ast.FormattedValue):
        return
    c.warn(LOC(e), f"{tp_name(infer_type(e)):.200} indices must be integers or slices, "
                   f"not {tp_name(index_type):.200}; perhaps you missed a comma?")


def is_import_originated(c, e):
    """Whether the global scope has an import named e, if it is a Name.  To
    avoid traversing the whole scope stack, only the global scope is checked."""
    if type(e) is not _ast.Name:
        return False
    return symtable.get_symbol(c.c_st.st_top, e.id) & symtable.DEF_IMPORT


def can_optimize_super_call(c, attr):
    e = attr.value
    if (type(e) is not _ast.Call or type(e.func) is not _ast.Name or e.func.id != "super"
            or attr.attr == "__class__" or len(e.keywords) != 0):
        return False
    num_args = len(e.args)
    super_name = e.func.id
    # detect statically-visible shadowing of 'super' name
    if symtable.get_scope(c.u.u_ste, super_name) != symtable.GLOBAL_IMPLICIT:
        return False
    if symtable.get_scope(c.c_st.st_top, super_name) != 0:
        return False
    if num_args == 2:
        # exactly two non-starred args; we can just load the provided args
        return not any(type(elt) is _ast.Starred for elt in e.args)
    if num_args != 0:
        return False
    # we need the following for zero-arg super():
    # enclosing function should have at least one argument
    md = c.u.u_metadata
    if md.u_argcount == 0 and md.u_posonlyargcount == 0:
        return False
    # __class__ cell should be available
    return c.get_ref_type("__class__") == symtable.FREE


def load_args_for_super(c, e):
    loc = LOC(e)
    # load super() global
    codegen_nameop(c, LOC(e.func), e.func.id, Load)
    if len(e.args) == 2:
        visit_expr(c, e.args[0])
        visit_expr(c, e.args[1])
        return
    # load __class__ cell
    assert c.get_ref_type("__class__") == symtable.FREE
    codegen_nameop(c, loc, "__class__", Load)
    # load self (first argument)
    codegen_nameop(c, loc, next(iter(c.u.u_metadata.u_varnames)), Load)


def update_start_location_to_match_attr(c, loc, attr):
    """If an attribute access spans multiple lines, update the current start
    location to point to the attribute name."""
    if loc.lineno != attr.end_lineno:
        lineno = attr.end_lineno
        end_lineno, col_offset, end_col_offset = loc.end_lineno, loc.col_offset, loc.end_col_offset
        length = len(attr.attr)
        if length <= attr.end_col_offset:
            col_offset = attr.end_col_offset - length
        else:
            # GH-94694: Somebody's compiling weird ASTs. Just drop the columns:
            col_offset = -1
            end_col_offset = -1
        # Make sure the end position still follows the start position, even
        # for weird ASTs:
        end_lineno = max(lineno, end_lineno)
        if lineno == end_lineno:
            end_col_offset = max(col_offset, end_col_offset)
        loc = SourceLocation(lineno, end_lineno, col_offset, end_col_offset)
    return loc


def maybe_optimize_function_call(c, e, end):
    args = e.args
    func = e.func
    if not (type(func) is _ast.Name and len(args) == 1 and len(e.keywords) == 0
            and type(args[0]) is _ast.GeneratorExp):
        return False
    generator_exp = args[0]
    if symtable.lookup(c.c_st, id(generator_exp)).ste_coroutine:
        return False
    loc = LOC(func)
    optimized = False
    skip_optimization = new_label(c)
    const_oparg = -1
    initial_res = None
    continue_jump_opcode = -1
    if func.id == "all":
        const_oparg = CONSTANT_BUILTIN_ALL
        initial_res = True
        continue_jump_opcode = POP_JUMP_IF_TRUE
    elif func.id == "any":
        const_oparg = CONSTANT_BUILTIN_ANY
        initial_res = False
        continue_jump_opcode = POP_JUMP_IF_FALSE
    elif func.id == "tuple":
        const_oparg = CONSTANT_BUILTIN_TUPLE
    if const_oparg != -1:
        addop_i(c, loc, COPY, 1)  # the function
        addop_i(c, loc, LOAD_COMMON_CONSTANT, const_oparg)
        codegen_addcompare(c, loc, _ast.Is())
        addop_j(c, loc, POP_JUMP_IF_FALSE, skip_optimization)
        addop(c, loc, POP_TOP)
        if const_oparg == CONSTANT_BUILTIN_TUPLE:
            addop_i(c, loc, BUILD_LIST, 0)
        visit_expr(c, generator_exp)
        loop = new_label(c)
        cleanup = new_label(c)
        use_label(c, loop)
        addop_j(c, loc, FOR_ITER, cleanup)
        if const_oparg == CONSTANT_BUILTIN_TUPLE:
            addop_i(c, loc, LIST_APPEND, 2)
            addop_j(c, loc, JUMP, loop)
        else:
            addop(c, loc, TO_BOOL)
            addop_j(c, loc, continue_jump_opcode, loop)
        addop(c, NO_LOCATION, POP_ITER)
        if const_oparg != CONSTANT_BUILTIN_TUPLE:
            addop_load_const(c, loc, initial_res is not True)
        addop_j(c, loc, JUMP, end)
        use_label(c, cleanup)
        addop(c, NO_LOCATION, END_FOR)
        addop(c, NO_LOCATION, POP_ITER)
        if const_oparg == CONSTANT_BUILTIN_TUPLE:
            addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_LIST_TO_TUPLE)
        else:
            addop_load_const(c, loc, initial_res)
        optimized = True
        addop_j(c, loc, JUMP, end)
    use_label(c, skip_optimization)
    return optimized


def maybe_optimize_method_call(c, e):
    """Whether the method call was optimized (and emitted)."""
    meth = e.func
    args = e.args
    kwds = e.keywords
    # Check that the call node is an attribute access
    if type(meth) is not _ast.Attribute or type(meth.ctx) is not Load:
        return False
    # Check that the base object is not something that is imported
    if is_import_originated(c, meth.value):
        return False
    # Check that there aren't too many arguments
    argsl = len(args)
    kwdsl = len(kwds)
    if argsl + kwdsl + (kwdsl != 0) >= STACK_USE_GUIDELINE:
        return False
    # Check that there are no *varargs types of arguments.
    if any(type(elt) is _ast.Starred for elt in args):
        return False
    if any(kw.arg is None for kw in kwds):
        return False
    # Alright, we can optimize the code.
    loc = LOC(meth)
    if can_optimize_super_call(c, meth):
        load_args_for_super(c, meth.value)
        opcode = LOAD_SUPER_METHOD if meth.value.args else LOAD_ZERO_SUPER_METHOD
        addop_name(c, loc, opcode, names(c), meth.attr)
        loc = update_start_location_to_match_attr(c, loc, meth)
        addop(c, loc, NOP)
    else:
        visit_expr(c, meth.value)
        loc = update_start_location_to_match_attr(c, loc, meth)
        addop_name(c, loc, LOAD_METHOD, names(c), meth.attr)
    visit_seq_expr(c, args)
    if kwdsl:
        for kw in kwds:
            visit_expr(c, kw.value)
        codegen_call_simple_kw_helper(c, loc, kwds)
        loc = update_start_location_to_match_attr(c, LOC(e), meth)
        addop_i(c, loc, CALL_KW, argsl + kwdsl)
    else:
        loc = update_start_location_to_match_attr(c, LOC(e), meth)
        addop_i(c, loc, CALL, argsl)
    return True


def codegen_validate_keywords(c, keywords):
    for i, key in enumerate(keywords):
        if key.arg is None:
            continue
        for other in keywords[i + 1:]:
            if other.arg and key.arg == other.arg:
                c.error(LOC(other), f"keyword argument repeated: {key.arg}")


def codegen_call(c, e):
    codegen_validate_keywords(c, e.keywords)
    if maybe_optimize_method_call(c, e):
        return
    skip_normal_call = new_label(c)
    check_caller(c, e.func)
    visit_expr(c, e.func)
    maybe_optimize_function_call(c, e, skip_normal_call)
    addop(c, LOC(e.func), PUSH_NULL)
    codegen_call_helper(c, LOC(e), 0, e.args, e.keywords)
    use_label(c, skip_normal_call)


def codegen_template_str(c, e):
    loc = LOC(e)
    last_was_interpolation = True
    stringslen = 0
    for value in e.values:
        if type(value) is _ast.Interpolation:
            if last_was_interpolation:
                addop_load_const(c, loc, "")
                stringslen += 1
            last_was_interpolation = True
        else:
            visit_expr(c, value)
            stringslen += 1
            last_was_interpolation = False
    if last_was_interpolation:
        addop_load_const(c, loc, "")
        stringslen += 1
    addop_i(c, loc, BUILD_TUPLE, stringslen)
    interpolationslen = 0
    for value in e.values:
        if type(value) is _ast.Interpolation:
            visit_expr(c, value)
            interpolationslen += 1
    addop_i(c, loc, BUILD_TUPLE, interpolationslen)
    addop(c, loc, BUILD_TEMPLATE)


def codegen_joined_str(c, e):
    loc = LOC(e)
    value_count = len(e.values)
    if value_count > STACK_USE_GUIDELINE:
        addop_load_const(c, loc, "")
        addop_name(c, loc, LOAD_METHOD, names(c), "join")
        addop_i(c, loc, BUILD_LIST, 0)
        for value in e.values:
            visit_expr(c, value)
            addop_i(c, loc, LIST_APPEND, 1)
        addop_i(c, loc, CALL, 1)
    else:
        visit_seq_expr(c, e.values)
        if value_count > 1:
            addop_i(c, loc, BUILD_STRING, value_count)
        elif value_count == 0:
            addop_load_const(c, loc, "")


FVC_STR, FVC_REPR, FVC_ASCII = 1, 2, 3   # Include/ceval.h
_conversions = {ord("s"): FVC_STR, ord("r"): FVC_REPR, ord("a"): FVC_ASCII}


def conversion_oparg(conversion):
    try:
        return _conversions[conversion]
    except KeyError:
        raise SystemError(f"Unrecognized conversion character {conversion}") from None


def codegen_interpolation(c, e):
    loc = LOC(e)
    visit_expr(c, e.value)
    addop_load_const(c, loc, e.str)
    oparg = 2
    if e.format_spec:
        oparg += 1
        visit_expr(c, e.format_spec)
    if e.conversion != -1:
        oparg |= conversion_oparg(e.conversion) << 2
    addop_i(c, loc, BUILD_INTERPOLATION, oparg)


def codegen_formatted_value(c, e):
    """Used to implement f-strings. Format a single value."""
    # The expression to be formatted.
    visit_expr(c, e.value)
    loc = LOC(e)
    if e.conversion != -1:
        addop_i(c, loc, CONVERT_VALUE, conversion_oparg(e.conversion))
    if e.format_spec:
        # Evaluate the format spec, and update our opcode arg.
        visit_expr(c, e.format_spec)
        addop(c, loc, FORMAT_WITH_SPEC)
    else:
        addop(c, loc, FORMAT_SIMPLE)


def codegen_subkwargs(c, loc, keywords, begin, end):
    n = end - begin
    assert n > 0
    big = n * 2 > STACK_USE_GUIDELINE
    if big:
        addop_i(c, NO_LOCATION, BUILD_MAP, 0)
    for kw in keywords[begin:end]:
        addop_load_const(c, loc, kw.arg)
        visit_expr(c, kw.value)
        if big:
            addop_i(c, NO_LOCATION, MAP_ADD, 1)
    if not big:
        addop_i(c, loc, BUILD_MAP, n)


def codegen_call_simple_kw_helper(c, loc, keywords):
    """Emit a tuple of keyword names before CALL_KW (codegen_call_helper and
    maybe_optimize_method_call)."""
    addop_load_const(c, loc, tuple(kw.arg for kw in keywords))


def codegen_call_helper_impl(c, loc, n, args, injected_arg, keywords):
    """Shared code between codegen_call and codegen_class; n args are already pushed."""
    codegen_validate_keywords(c, keywords)
    nelts = len(args)
    nkwelts = len(keywords)
    if (nelts + nkwelts * 2 <= STACK_USE_GUIDELINE
            and not any(type(elt) is _ast.Starred for elt in args)
            and not any(kw.arg is None for kw in keywords)):
        # No * or ** args, so can use faster calling sequence
        for elt in args:
            visit_expr(c, elt)
        if injected_arg:
            codegen_nameop(c, loc, injected_arg, Load)
            nelts += 1
        if nkwelts:
            for kw in keywords:
                visit_expr(c, kw.value)
            codegen_call_simple_kw_helper(c, loc, keywords)
            addop_i(c, loc, CALL_KW, n + nelts + nkwelts)
        else:
            addop_i(c, loc, CALL, n + nelts)
        return

    # ex_call: Do positional arguments.
    if n == 0 and nelts == 1 and type(args[0]) is _ast.Starred:
        visit_expr(c, args[0].value)
    else:
        starunpack_helper(c, loc, args, n, BUILD_LIST, LIST_APPEND, LIST_EXTEND, 1, injected_arg)
    # Then keyword arguments
    if nkwelts:
        # Has a new dict been pushed
        have_dict = False
        nseen = 0  # the number of keyword arguments on the stack following
        for i, kw in enumerate(keywords):
            if kw.arg is None:
                # A keyword argument unpacking.
                if nseen:
                    codegen_subkwargs(c, loc, keywords, i - nseen, i)
                    if have_dict:
                        addop_i(c, loc, DICT_MERGE, 1)
                    have_dict = True
                    nseen = 0
                if not have_dict:
                    addop_i(c, loc, BUILD_MAP, 0)
                    have_dict = True
                visit_expr(c, kw.value)
                addop_i(c, loc, DICT_MERGE, 1)
            else:
                nseen += 1
        if nseen:
            # Pack up any trailing keyword arguments.
            codegen_subkwargs(c, loc, keywords, nkwelts - nseen, nkwelts)
            if have_dict:
                addop_i(c, loc, DICT_MERGE, 1)
            have_dict = True
        assert have_dict
    if nkwelts == 0:
        addop(c, loc, PUSH_NULL)
    addop(c, loc, CALL_FUNCTION_EX)


def codegen_call_helper(c, loc, n, args, keywords):
    codegen_call_helper_impl(c, loc, n, args, None, keywords)


# List and set comprehensions work by being inlined at the location where
# they are defined. The isolation of iteration variables is provided by
# pushing/popping clashing locals on the stack. Generator expressions work by
# creating a nested function to perform the actual iteration.  This means
# that the iteration variables don't leak into the current scope.  See
# https://peps.python.org/pep-0709/ for additional information.  The defined
# function is called immediately following its definition, with the result of
# that call being the result of the expression.  The LC/SC version returns
# the populated container, while the GE version is flagged in symtable.py as
# a generator, so it returns the generator object when the function is
# called.

def codegen_comprehension_generator(c, loc, generators, gen_index, depth, elt, val, type_,
                                    iter_on_stack):
    gen = generators[gen_index]
    if gen.is_async:
        codegen_async_comprehension_generator(c, loc, generators, gen_index, depth, elt, val,
                                              type_, iter_on_stack)
    else:
        codegen_sync_comprehension_generator(c, loc, generators, gen_index, depth, elt, val,
                                             type_, iter_on_stack)


def codegen_comprehension_element(c, elt, val, type_, depth):
    """Comprehension specific code after the last for generator; its location."""
    elt_loc = LOC(elt)
    if type_ == COMP_GENEXP:
        visit_expr(c, elt)
        codegen_addop_yield(c, elt_loc)
        addop(c, elt_loc, POP_TOP)
    elif type_ == COMP_LISTCOMP:
        visit_expr(c, elt)
        addop_i(c, elt_loc, LIST_APPEND, depth + 1)
    elif type_ == COMP_SETCOMP:
        visit_expr(c, elt)
        addop_i(c, elt_loc, SET_ADD, depth + 1)
    else:
        assert type_ == COMP_DICTCOMP
        # With '{k: v}', k is evaluated before v, so we do the same.
        visit_expr(c, elt)
        visit_expr(c, val)
        elt_loc = LOCATION(elt.lineno, val.end_lineno, elt.col_offset, val.end_col_offset)
        addop_i(c, elt_loc, MAP_ADD, depth + 1)
    return elt_loc


def codegen_sync_comprehension_generator(c, loc, generators, gen_index, depth, elt, val, type_,
                                         iter_on_stack):
    # generate code for the iterator, then each of the ifs, and then write
    # to the element
    start = new_label(c)
    if_cleanup = new_label(c)
    anchor = new_label(c)
    gen = generators[gen_index]
    if not iter_on_stack:
        if gen_index == 0:
            assert c.u.u_metadata.u_argcount == 1
            addop_i(c, loc, LOAD_FAST, 0)
        else:
            # Sub-iter - calculate on the fly
            # Fast path for the temporary variable assignment idiom:
            #     for y in [f(x)]
            it = gen.iter
            elts = it.elts if type(it) in (_ast.List, _ast.Tuple) else ()
            if len(elts) == 1:
                if type(elts[0]) is not _ast.Starred:
                    visit_expr(c, elts[0])
                    start = NO_LABEL
            if start != NO_LABEL:
                visit_expr(c, gen.iter)
                addop(c, LOC(gen.iter), GET_ITER)
    if start != NO_LABEL:
        depth += 1
        use_label(c, start)
        addop_j(c, LOC(gen.iter), FOR_ITER, anchor)
    visit_expr(c, gen.target)
    # XXX this needs to be cleaned up...a lot!
    for e in gen.ifs:
        codegen_jump_if(c, loc, e, if_cleanup, 0)
    gen_index += 1
    if gen_index < len(generators):
        codegen_comprehension_generator(c, loc, generators, gen_index, depth, elt, val, type_, 0)
    elt_loc = LOC(elt)
    # only append after the last for generator
    if gen_index >= len(generators):
        elt_loc = codegen_comprehension_element(c, elt, val, type_, depth)
    use_label(c, if_cleanup)
    if start != NO_LABEL:
        addop_j(c, elt_loc, JUMP, start)
        use_label(c, anchor)
        # It is important for instrumentation that the `END_FOR` comes first.
        # Iteration over a generator will jump to the first of these
        # instructions, but a non-generator will jump to a later instruction.
        addop(c, NO_LOCATION, END_FOR)
        addop(c, NO_LOCATION, POP_ITER)


def codegen_async_comprehension_generator(c, loc, generators, gen_index, depth, elt, val,
                                          type_, iter_on_stack):
    start = new_label(c)
    send = new_label(c)
    except_ = new_label(c)
    if_cleanup = new_label(c)
    gen = generators[gen_index]
    if not iter_on_stack:
        if gen_index == 0:
            assert c.u.u_metadata.u_argcount == 1
            addop_i(c, loc, LOAD_FAST, 0)
        else:
            # Sub-iter - calculate on the fly
            visit_expr(c, gen.iter)
            addop(c, LOC(gen.iter), GET_AITER)
    use_label(c, start)
    # Runtime will push a block here, so we need to account for that
    c.push_fblock(loc, COMPILE_FBLOCK_ASYNC_COMPREHENSION_GENERATOR, start, NO_LABEL, None)
    addop_j(c, loc, SETUP_FINALLY, except_)
    addop(c, loc, GET_ANEXT)
    addop_load_const(c, loc, None)
    use_label(c, send)
    codegen_add_yield_from(c, loc, 1)
    addop(c, loc, POP_BLOCK)
    visit_expr(c, gen.target)
    for e in gen.ifs:
        codegen_jump_if(c, loc, e, if_cleanup, 0)
    depth += 1
    gen_index += 1
    if gen_index < len(generators):
        codegen_comprehension_generator(c, loc, generators, gen_index, depth, elt, val, type_, 0)
    elt_loc = LOC(elt)
    # only append after the last for generator
    if gen_index >= len(generators):
        elt_loc = codegen_comprehension_element(c, elt, val, type_, depth)
    use_label(c, if_cleanup)
    addop_j(c, elt_loc, JUMP, start)
    c.pop_fblock(COMPILE_FBLOCK_ASYNC_COMPREHENSION_GENERATOR, start)
    use_label(c, except_)
    addop_j(c, loc, END_ASYNC_FOR, send)


def codegen_push_inlined_comprehension_locals(c, loc, comp, state):
    outer = c.u.u_ste
    in_class_block = outer.ste_type == symtable.ClassBlock and not c.u.u_in_inlined_comp
    md = c.u.u_metadata
    # iterate over names bound in the comprehension and ensure we isolate
    # them from the outer scope as needed
    for k, symbol in comp.ste_symbols.items():
        scope = symtable.SYMBOL_TO_SCOPE(symbol)
        outsc = symtable.SYMBOL_TO_SCOPE(symtable.get_symbol(outer, k))
        if (symbol & symtable.DEF_LOCAL and not symbol & symtable.DEF_NONLOCAL) or in_class_block:
            # local names bound in comprehension must be isolated from outer
            # scope; push existing value (which may be NULL if not defined)
            # on stack
            if state.pushed_locals is None:
                state.pushed_locals = []
            # in the case of a cell, this will actually push the cell itself
            # to the stack, then we'll create a new one for the comprehension
            # and restore the original one after
            addop_name(c, loc, LOAD_FAST_AND_CLEAR, md.u_varnames, k)
            if scope == symtable.CELL:
                if outsc == symtable.FREE:
                    addop_name(c, loc, MAKE_CELL, md.u_freevars, k)
                else:
                    addop_name(c, loc, MAKE_CELL, md.u_cellvars, k)
            state.pushed_locals.append(k)
    if state.pushed_locals:
        # Outermost iterable expression was already evaluated and is on the
        # stack, we need to swap it back to TOS. This also rotates the order
        # of `pushed_locals` on the stack, but this will be reversed when we
        # swap out the comprehension result in pop_inlined_comprehension_state
        addop_i(c, loc, SWAP, len(state.pushed_locals) + 1)
        # Add our own cleanup handler to restore comprehension locals in case
        # of exception, so they have the correct values inside an exception
        # handler or finally block.
        cleanup = new_label(c)
        state.cleanup = cleanup
        # Count against CO_MAXBLOCKS: SETUP_FINALLY consumes an except-stack
        # slot even though return/continue/break cannot appear here.
        c.push_fblock(loc, COMPILE_FBLOCK_INLINED_COMPREHENSION, cleanup, NO_LABEL, None)
        addop_j(c, loc, SETUP_FINALLY, cleanup)


def push_inlined_comprehension_state(c, loc, comp, state):
    c.tweak_inlined_comprehension_scopes(loc, comp, state)
    codegen_push_inlined_comprehension_locals(c, loc, comp, state)


def restore_inlined_comprehension_locals(c, loc, state):
    # pop names we pushed to stack earlier
    npops = len(state.pushed_locals)
    # Preserve the comprehension result (or exception) as TOS. This reverses
    # the SWAP we did in push_inlined_comprehension_state to get the
    # outermost iterable to TOS, so we can still just iterate pushed_locals
    # in simple reverse order
    addop_i(c, loc, SWAP, npops + 1)
    for k in reversed(state.pushed_locals):
        addop_name(c, loc, STORE_FAST_MAYBE_NULL, c.u.u_metadata.u_varnames, k)


def codegen_pop_inlined_comprehension_locals(c, loc, state):
    if state.pushed_locals:
        addop(c, NO_LOCATION, POP_BLOCK)
        c.pop_fblock(COMPILE_FBLOCK_INLINED_COMPREHENSION, state.cleanup)
        end = new_label(c)
        addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, end)
        # cleanup from an exception inside the comprehension
        use_label(c, state.cleanup)
        # discard incomplete comprehension result (beneath exc on stack)
        addop_i(c, NO_LOCATION, SWAP, 2)
        addop(c, NO_LOCATION, POP_TOP)
        restore_inlined_comprehension_locals(c, loc, state)
        addop_i(c, NO_LOCATION, RERAISE, 0)
        use_label(c, end)
        restore_inlined_comprehension_locals(c, loc, state)
        state.pushed_locals = None


def pop_inlined_comprehension_state(c, loc, state):
    codegen_pop_inlined_comprehension_locals(c, loc, state)
    c.revert_inlined_comprehension_scopes(loc, state)


def codegen_comprehension_iter(c, comp):
    visit_expr(c, comp.iter)
    addop(c, LOC(comp.iter), GET_AITER if comp.is_async else GET_ITER)


_comprehension_builds = {COMP_LISTCOMP: BUILD_LIST, COMP_SETCOMP: BUILD_SET,
                         COMP_DICTCOMP: BUILD_MAP}


def codegen_comprehension(c, e, type_, name, generators, elt, val):
    inline_state = InlinedComprehensionState()
    entry = symtable.lookup(c.c_st, id(e))
    is_inlined = entry.ste_comp_inlined
    is_async_comprehension = entry.ste_coroutine
    loc = LOC(e)
    outermost = generators[0]
    if is_inlined:
        codegen_comprehension_iter(c, outermost)
        push_inlined_comprehension_state(c, loc, entry, inline_state)
    else:
        # Receive outermost iter as an implicit argument
        umd = CodeUnitMetadata(u_argcount=1)
        codegen_enter_scope(c, name, COMPILE_SCOPE_COMPREHENSION, id(e), e.lineno, None, umd)
    if type_ != COMP_GENEXP:
        addop_i(c, loc, _comprehension_builds[type_], 0)
        if is_inlined:
            addop_i(c, loc, SWAP, 2)
    codegen_comprehension_generator(c, loc, generators, 0, 0, elt, val, type_, is_inlined)
    if is_inlined:
        pop_inlined_comprehension_state(c, loc, inline_state)
        return
    if type_ != COMP_GENEXP:
        addop(c, LOC(e), RETURN_VALUE)
    if type_ == COMP_GENEXP:
        codegen_wrap_in_stopiteration_handler(c)
    co = c.optimize_and_assemble(1)
    c.exit_scope()
    loc = LOC(e)
    codegen_make_closure(c, loc, co, 0)
    codegen_comprehension_iter(c, outermost)
    addop_i(c, loc, CALL, 0)
    if is_async_comprehension and type_ != COMP_GENEXP:
        addop_i(c, loc, GET_AWAITABLE, 0)
        addop_load_const(c, loc, None)
        codegen_add_yield_from(c, loc, 1)


def codegen_genexp(c, e):
    codegen_comprehension(c, e, COMP_GENEXP, "<genexpr>", e.generators, e.elt, None)


def codegen_listcomp(c, e):
    codegen_comprehension(c, e, COMP_LISTCOMP, "<listcomp>", e.generators, e.elt, None)


def codegen_setcomp(c, e):
    codegen_comprehension(c, e, COMP_SETCOMP, "<setcomp>", e.generators, e.elt, None)


def codegen_dictcomp(c, e):
    codegen_comprehension(c, e, COMP_DICTCOMP, "<dictcomp>", e.generators, e.key, e.value)


def codegen_with_except_finish(c, cleanup):
    suppress = new_label(c)
    addop(c, NO_LOCATION, TO_BOOL)
    addop_j(c, NO_LOCATION, POP_JUMP_IF_TRUE, suppress)
    addop_i(c, NO_LOCATION, RERAISE, 2)
    use_label(c, suppress)
    addop(c, NO_LOCATION, POP_TOP)  # exc_value
    addop(c, NO_LOCATION, POP_BLOCK)
    addop(c, NO_LOCATION, POP_EXCEPT)
    addop(c, NO_LOCATION, POP_TOP)
    addop(c, NO_LOCATION, POP_TOP)
    addop(c, NO_LOCATION, POP_TOP)
    exit = new_label(c)
    addop_j(c, NO_LOCATION, JUMP_NO_INTERRUPT, exit)
    use_label(c, cleanup)
    codegen_pop_except_and_reraise(c, NO_LOCATION)
    use_label(c, exit)


def codegen_async_with_inner(c, s, pos):
    """Implements the async with statement.

    async with EXPR as VAR:
        BLOCK

    is implemented roughly as:

    context = EXPR
    exit = context.__aexit__  # not calling it
    value = await context.__aenter__()
    try:
        VAR = value  # if VAR present in the syntax
        BLOCK
    finally:
        if an exception was raised:
            exc = copy of (exception, instance, traceback)
        else:
            exc = (None, None, None)
        if not (await exit(*exc)):
            raise
    """
    item = s.items[pos]
    block = new_label(c)
    final = new_label(c)
    exit = new_label(c)
    cleanup = new_label(c)
    # Evaluate EXPR
    visit_expr(c, item.context_expr)
    loc = LOC(item.context_expr)
    addop_i(c, loc, COPY, 1)
    addop_i(c, loc, LOAD_SPECIAL, SPECIAL___AEXIT__)
    addop_i(c, loc, SWAP, 2)
    addop_i(c, loc, SWAP, 3)
    addop_i(c, loc, LOAD_SPECIAL, SPECIAL___AENTER__)
    addop_i(c, loc, CALL, 0)
    addop_i(c, loc, GET_AWAITABLE, 1)
    addop_load_const(c, loc, None)
    codegen_add_yield_from(c, loc, 1)
    addop_j(c, loc, SETUP_WITH, final)
    # SETUP_WITH pushes a finally block.
    use_label(c, block)
    c.push_fblock(loc, COMPILE_FBLOCK_ASYNC_WITH, block, final, s)
    if item.optional_vars:
        visit_expr(c, item.optional_vars)
    else:
        # Discard result from context.__aenter__()
        addop(c, loc, POP_TOP)
    pos += 1
    if pos == len(s.items):
        # BLOCK code
        visit_seq_stmt(c, s.body)
    else:
        codegen_async_with_inner(c, s, pos)
    c.pop_fblock(COMPILE_FBLOCK_ASYNC_WITH, block)
    addop(c, loc, POP_BLOCK)
    # End of body; start the cleanup
    # For successful outcome: call __exit__(None, None, None)
    codegen_call_exit_with_nones(c, loc)
    addop_i(c, loc, GET_AWAITABLE, 2)
    addop_load_const(c, loc, None)
    codegen_add_yield_from(c, loc, 1)
    addop(c, loc, POP_TOP)
    addop_j(c, loc, JUMP, exit)
    # For exceptional outcome:
    use_label(c, final)
    addop_j(c, loc, SETUP_CLEANUP, cleanup)
    addop(c, loc, PUSH_EXC_INFO)
    addop(c, loc, WITH_EXCEPT_START)
    addop_i(c, loc, GET_AWAITABLE, 2)
    addop_load_const(c, loc, None)
    codegen_add_yield_from(c, loc, 1)
    codegen_with_except_finish(c, cleanup)
    use_label(c, exit)


def codegen_async_with(c, s):
    codegen_async_with_inner(c, s, 0)


def codegen_with_inner(c, s, pos):
    """Implements the with statement from PEP 343.

    with EXPR as VAR:
        BLOCK

    is implemented as:
         <code for EXPR>
         SETUP_WITH  E
         <code to store to VAR> or POP_TOP
         <code for BLOCK>
         LOAD_CONST (None, None, None)
         CALL_FUNCTION_EX 0
         JUMP  EXIT
     E:  WITH_EXCEPT_START (calls EXPR.__exit__)
         POP_JUMP_IF_TRUE T:
         RERAISE
     T:  POP_TOP (remove exception from stack)
         POP_EXCEPT
         POP_TOP
     EXIT:
    """
    item = s.items[pos]
    block = new_label(c)
    final = new_label(c)
    exit = new_label(c)
    cleanup = new_label(c)
    # Evaluate EXPR
    visit_expr(c, item.context_expr)
    # Will push bound __exit__
    loc = LOC(item.context_expr)
    addop_i(c, loc, COPY, 1)
    addop_i(c, loc, LOAD_SPECIAL, SPECIAL___EXIT__)
    addop_i(c, loc, SWAP, 2)
    addop_i(c, loc, SWAP, 3)
    addop_i(c, loc, LOAD_SPECIAL, SPECIAL___ENTER__)
    addop_i(c, loc, CALL, 0)
    addop_j(c, loc, SETUP_WITH, final)
    # SETUP_WITH pushes a finally block.
    use_label(c, block)
    c.push_fblock(loc, COMPILE_FBLOCK_WITH, block, final, s)
    if item.optional_vars:
        visit_expr(c, item.optional_vars)
    else:
        # Discard result from context.__enter__()
        addop(c, loc, POP_TOP)
    pos += 1
    if pos == len(s.items):
        # BLOCK code
        visit_seq_stmt(c, s.body)
    else:
        codegen_with_inner(c, s, pos)
    addop(c, NO_LOCATION, POP_BLOCK)
    c.pop_fblock(COMPILE_FBLOCK_WITH, block)
    # End of body; start the cleanup.
    # For successful outcome: call __exit__(None, None, None)
    codegen_call_exit_with_nones(c, loc)
    addop(c, loc, POP_TOP)
    addop_j(c, loc, JUMP, exit)
    # For exceptional outcome:
    use_label(c, final)
    addop_j(c, loc, SETUP_CLEANUP, cleanup)
    addop(c, loc, PUSH_EXC_INFO)
    addop(c, loc, WITH_EXCEPT_START)
    codegen_with_except_finish(c, cleanup)
    use_label(c, exit)


def codegen_with(c, s):
    codegen_with_inner(c, s, 0)


def codegen_yield(c, e, loc):
    if not symtable.is_function_like(c.u.u_ste):
        c.error(loc, "'yield' outside function")
    if e.value:
        visit_expr(c, e.value)
    else:
        addop_load_const(c, loc, None)
    codegen_addop_yield(c, loc)


def codegen_yield_from(c, e, loc):
    if not symtable.is_function_like(c.u.u_ste):
        c.error(loc, "'yield from' outside function")
    if c.u.u_scope_type == COMPILE_SCOPE_ASYNC_FUNCTION:
        c.error(loc, "'yield from' inside async function")
    visit_expr(c, e.value)
    addop(c, loc, GET_YIELD_FROM_ITER)
    addop_load_const(c, loc, None)
    codegen_add_yield_from(c, loc, 0)


def codegen_await(c, e, loc):
    visit_expr(c, e.value)
    addop_i(c, loc, GET_AWAITABLE, 0)
    addop_load_const(c, loc, None)
    codegen_add_yield_from(c, loc, 1)


def codegen_named_expr(c, e, loc):
    visit_expr(c, e.value)
    addop_i(c, loc, COPY, 1)
    visit_expr(c, e.target)


def codegen_binop(c, e, loc):
    visit_expr(c, e.left)
    visit_expr(c, e.right)
    addop_binary(c, loc, e.op, False)


def codegen_unaryop(c, e, loc):
    visit_expr(c, e.operand)
    t = type(e.op)
    if t is _ast.UAdd:
        addop_i(c, loc, CALL_INTRINSIC_1, INTRINSIC_UNARY_POSITIVE)
    elif t is _ast.Not:
        addop(c, loc, TO_BOOL)
        addop(c, loc, UNARY_NOT)
    else:
        addop(c, loc, unaryop(e.op))


def codegen_attribute(c, e, loc):
    ctx = type(e.ctx)
    if ctx is Load and can_optimize_super_call(c, e):
        load_args_for_super(c, e.value)
        opcode = LOAD_SUPER_ATTR if e.value.args else LOAD_ZERO_SUPER_ATTR
        addop_name(c, loc, opcode, names(c), e.attr)
        loc = update_start_location_to_match_attr(c, loc, e)
        addop(c, loc, NOP)
        return
    c.maybe_add_static_attribute_to_class(e)
    visit_expr(c, e.value)
    loc = update_start_location_to_match_attr(c, LOC(e), e)
    op = LOAD_ATTR if ctx is Load else STORE_ATTR if ctx is Store else DELETE_ATTR
    addop_name(c, loc, op, names(c), e.attr)


def codegen_starred(c, e, loc):
    if type(e.ctx) is Store:
        # In all legitimate cases, the Starred node was already replaced by
        # codegen_list/codegen_tuple. XXX: is that okay?
        c.error(loc, "starred assignment target must be in a list or tuple")
    c.error(loc, "can't use starred expression here")


_expr_visitors = {
    _ast.NamedExpr: codegen_named_expr,
    _ast.BoolOp: lambda c, e, loc: codegen_boolop(c, e),
    _ast.BinOp: codegen_binop,
    _ast.UnaryOp: codegen_unaryop,
    _ast.Lambda: lambda c, e, loc: codegen_lambda(c, e),
    _ast.IfExp: lambda c, e, loc: codegen_ifexp(c, e),
    _ast.Dict: lambda c, e, loc: codegen_dict(c, e),
    _ast.Set: lambda c, e, loc: codegen_set(c, e),
    _ast.GeneratorExp: lambda c, e, loc: codegen_genexp(c, e),
    _ast.ListComp: lambda c, e, loc: codegen_listcomp(c, e),
    _ast.SetComp: lambda c, e, loc: codegen_setcomp(c, e),
    _ast.DictComp: lambda c, e, loc: codegen_dictcomp(c, e),
    _ast.Yield: codegen_yield,
    _ast.YieldFrom: codegen_yield_from,
    _ast.Await: codegen_await,
    _ast.Compare: lambda c, e, loc: codegen_compare(c, e),
    _ast.Call: lambda c, e, loc: codegen_call(c, e),
    _ast.Constant: lambda c, e, loc: addop_load_const(c, loc, e.value),
    _ast.JoinedStr: lambda c, e, loc: codegen_joined_str(c, e),
    _ast.TemplateStr: lambda c, e, loc: codegen_template_str(c, e),
    _ast.FormattedValue: lambda c, e, loc: codegen_formatted_value(c, e),
    _ast.Interpolation: lambda c, e, loc: codegen_interpolation(c, e),
    # The following exprs can be assignment targets.
    _ast.Attribute: codegen_attribute,
    _ast.Subscript: lambda c, e, loc: codegen_subscript(c, e),
    _ast.Starred: codegen_starred,
    _ast.Slice: lambda c, e, loc: codegen_slice(c, e),
    _ast.Name: lambda c, e, loc: codegen_nameop(c, loc, e.id, type(e.ctx)),
    # child nodes of List and Tuple will have expr_context set
    _ast.List: lambda c, e, loc: codegen_list(c, e),
    _ast.Tuple: lambda c, e, loc: codegen_tuple(c, e),
}


def visit_expr(c, e):
    """codegen_visit_expr."""
    _expr_visitors[type(e)](c, e, LOC(e))


def is_constant_slice(s):
    return (type(s) is _ast.Slice
            and (s.lower is None or type(s.lower) is _ast.Constant)
            and (s.upper is None or type(s.upper) is _ast.Constant)
            and (s.step is None or type(s.step) is _ast.Constant))


def should_apply_two_element_slice_optimization(s):
    return not is_constant_slice(s) and type(s) is _ast.Slice and s.step is None


def codegen_augassign(c, s):
    e = s.target
    loc = LOC(e)
    t = type(e)
    if t is _ast.Attribute:
        visit_expr(c, e.value)
        addop_i(c, loc, COPY, 1)
        loc = update_start_location_to_match_attr(c, loc, e)
        addop_name(c, loc, LOAD_ATTR, names(c), e.attr)
    elif t is _ast.Subscript:
        visit_expr(c, e.value)
        if should_apply_two_element_slice_optimization(e.slice):
            codegen_slice_two_parts(c, e.slice)
            addop_i(c, loc, COPY, 3)
            addop_i(c, loc, COPY, 3)
            addop_i(c, loc, COPY, 3)
            addop(c, loc, BINARY_SLICE)
        else:
            visit_expr(c, e.slice)
            addop_i(c, loc, COPY, 2)
            addop_i(c, loc, COPY, 2)
            addop_i(c, loc, BINARY_OP, NB_SUBSCR)
    elif t is _ast.Name:
        codegen_nameop(c, loc, e.id, Load)
    else:
        raise SystemError(f"invalid node type ({t.__name__}) for augmented assignment")
    loc = LOC(s)
    visit_expr(c, s.value)
    addop_binary(c, loc, s.op, True)
    loc = LOC(e)
    if t is _ast.Attribute:
        loc = update_start_location_to_match_attr(c, loc, e)
        addop_i(c, loc, SWAP, 2)
        addop_name(c, loc, STORE_ATTR, names(c), e.attr)
    elif t is _ast.Subscript:
        if should_apply_two_element_slice_optimization(e.slice):
            addop_i(c, loc, SWAP, 4)
            addop_i(c, loc, SWAP, 3)
            addop_i(c, loc, SWAP, 2)
            addop(c, loc, STORE_SLICE)
        else:
            addop_i(c, loc, SWAP, 3)
            addop_i(c, loc, SWAP, 2)
            addop(c, loc, STORE_SUBSCR)
    else:
        codegen_nameop(c, loc, e.id, Store)


def codegen_check_ann_expr(c, e):
    visit_expr(c, e)
    addop(c, LOC(e), POP_TOP)


def codegen_check_annotation(c, s):
    # Annotations of complex targets does not produce anything under
    # annotations future
    if c.c_future.ff_features & CO_FUTURE_ANNOTATIONS:
        return
    # Annotations are only evaluated in a module or class.
    if c.u.u_scope_type in (COMPILE_SCOPE_MODULE, COMPILE_SCOPE_CLASS):
        codegen_check_ann_expr(c, s.annotation)


def codegen_check_ann_subscr(c, e):
    """We check that everything in a subscript is defined at runtime."""
    t = type(e)
    if t is _ast.Slice:
        if e.lower:
            codegen_check_ann_expr(c, e.lower)
        if e.upper:
            codegen_check_ann_expr(c, e.upper)
        if e.step:
            codegen_check_ann_expr(c, e.step)
    elif t is _ast.Tuple:
        # extended slice
        for elt in e.elts:
            codegen_check_ann_subscr(c, elt)
    else:
        codegen_check_ann_expr(c, e)


def codegen_annassign(c, s):
    loc = LOC(s)
    targ = s.target
    future_annotations = c.c_future.ff_features & CO_FUTURE_ANNOTATIONS
    # We perform the actual assignment first.
    if s.value:
        visit_expr(c, s.value)
        visit_expr(c, targ)
    t = type(targ)
    if t is _ast.Name:
        # If we have a simple name in a module or class, store annotation.
        if s.simple and c.u.u_scope_type in (COMPILE_SCOPE_MODULE, COMPILE_SCOPE_CLASS):
            if future_annotations:
                codegen_visit_annexpr(c, s.annotation)
                addop_name(c, loc, LOAD_NAME, names(c), "__annotations__")
                addop_load_const(c, loc, c.maybe_mangle(targ.id))
                addop(c, loc, STORE_SUBSCR)
            else:
                conditional_annotation_index = c.add_deferred_annotation(s)
                if conditional_annotation_index is not None:
                    if c.u.u_scope_type == COMPILE_SCOPE_CLASS:
                        addop_name(c, loc, LOAD_DEREF, c.u.u_metadata.u_cellvars,
                                   "__conditional_annotations__")
                    else:
                        addop_name(c, loc, LOAD_NAME, names(c), "__conditional_annotations__")
                    addop_load_const(c, loc, conditional_annotation_index)
                    addop_i(c, loc, SET_ADD, 1)
                    addop(c, loc, POP_TOP)
    elif t is _ast.Attribute:
        if not s.value:
            codegen_check_ann_expr(c, targ.value)
    elif t is _ast.Subscript:
        if not s.value:
            codegen_check_ann_expr(c, targ.value)
            codegen_check_ann_subscr(c, targ.slice)
    else:
        raise SystemError(f"invalid node type ({t.__name__}) for annotated assignment")
    # Annotation is evaluated last.
    if future_annotations and not s.simple:
        codegen_check_annotation(c, s)


def codegen_subscript(c, e):
    loc = LOC(e)
    ctx = type(e.ctx)
    if ctx is Load:
        check_subscripter(c, e.value)
        check_index(c, e.value, e.slice)
    visit_expr(c, e.value)
    if should_apply_two_element_slice_optimization(e.slice) and ctx is not Del:
        codegen_slice_two_parts(c, e.slice)
        if ctx is Load:
            addop(c, loc, BINARY_SLICE)
        else:
            assert ctx is Store
            addop(c, loc, STORE_SLICE)
    else:
        visit_expr(c, e.slice)
        if ctx is Load:
            addop_i(c, loc, BINARY_OP, NB_SUBSCR)
        elif ctx is Store:
            addop(c, loc, STORE_SUBSCR)
        else:
            addop(c, loc, DELETE_SUBSCR)


def codegen_slice_two_parts(c, s):
    if s.lower:
        visit_expr(c, s.lower)
    else:
        addop_load_const(c, LOC(s), None)
    if s.upper:
        visit_expr(c, s.upper)
    else:
        addop_load_const(c, LOC(s), None)


def codegen_slice(c, s):
    n = 2
    if is_constant_slice(s):
        start = s.lower.value if s.lower else None
        stop = s.upper.value if s.upper else None
        step = s.step.value if s.step else None
        addop_load_const(c, LOC(s), slice(start, stop, step))
        return
    codegen_slice_two_parts(c, s)
    if s.step:
        n += 1
        visit_expr(c, s.step)
    addop_i(c, LOC(s), BUILD_SLICE, n)


# PEP 634: Structural Pattern Matching
#
# To keep things simple, all codegen_pattern_* routines follow the convention
# of consuming TOS (the subject for the given pattern) and calling
# jump_to_fail_pop on failure (no match).
#
# When calling into these routines, it's important that pc.on_top be kept
# updated to reflect the current number of items that we are using on the
# top of the stack: they will be popped on failure, and any name captures
# will be stored *underneath* them on success. This lets us defer all names
# stores until the *entire* pattern matches.

class PatternContext:
    """pattern_context."""

    def __init__(self):
        # A list of strings corresponding to name captures. It is used to track:
        # - Repeated name assignments in the same pattern.
        # - Different name assignments in alternatives.
        # - The order of name assignments in alternatives.
        self.stores = []
        # If 0, any name captures against our subject will raise.
        self.allow_irrefutable = 0
        # Labels to jump to on failure. Jumping to fail_pop[i] will pop i
        # items off of the stack. The end result looks like this (with each
        # block falling through to the next):
        # fail_pop[4]: POP_TOP
        # fail_pop[3]: POP_TOP
        # fail_pop[2]: POP_TOP
        # fail_pop[1]: POP_TOP
        # fail_pop[0]: NOP
        self.fail_pop = []
        # The number of items on top of the stack that need to *stay* on top
        # of the stack. Variable captures go beneath these. All of them will
        # be popped on failure.
        self.on_top = 0


def WILDCARD_CHECK(n):
    return type(n) is _ast.MatchAs and not n.name


def WILDCARD_STAR_CHECK(n):
    return type(n) is _ast.MatchStar and not n.name


def MATCH_VALUE_EXPR(n):
    """Limit permitted subexpressions, even if the parser & AST validator let them through."""
    return type(n) in (_ast.Constant, _ast.Attribute)


def ensure_fail_pop(c, pc, n):
    """Make pc.fail_pop long enough for n items to be popped on failure."""
    while len(pc.fail_pop) < n + 1:
        pc.fail_pop.append(new_label(c))


def jump_to_fail_pop(c, loc, pc, op):
    """Use op to jump to the correct fail_pop block."""
    # Pop any items on the top of the stack, plus any objects we were going
    # to capture on success:
    pops = pc.on_top + len(pc.stores)
    ensure_fail_pop(c, pc, pops)
    addop_j(c, loc, op, pc.fail_pop[pops])


def emit_and_reset_fail_pop(c, loc, pc):
    """Build all of the fail_pop blocks and reset fail_pop."""
    if not pc.fail_pop:
        return
    for lbl in reversed(pc.fail_pop[1:]):
        use_label(c, lbl)
        addop(c, loc, POP_TOP)
    use_label(c, pc.fail_pop[0])
    pc.fail_pop = []


def codegen_error_duplicate_store(c, loc, n):
    c.error(loc, f"multiple assignments to name {n!r} in pattern")


def codegen_pattern_helper_rotate(c, loc, count):
    """Duplicate the effect of 3.10's ROT_* instructions using SWAPs."""
    while 1 < count:
        addop_i(c, loc, SWAP, count)
        count -= 1


def codegen_pattern_helper_store_name(c, loc, n, pc):
    if n is None:
        addop(c, loc, POP_TOP)
        return
    # Can't assign to the same name twice:
    if n in pc.stores:
        codegen_error_duplicate_store(c, loc, n)
    # Rotate this object underneath any items we need to preserve:
    rotations = pc.on_top + len(pc.stores) + 1
    codegen_pattern_helper_rotate(c, loc, rotations)
    pc.stores.append(n)


def codegen_pattern_unpack_helper(c, loc, elts):
    n = len(elts)
    seen_star = False
    for i, elt in enumerate(elts):
        if type(elt) is _ast.MatchStar and not seen_star:
            if i >= (1 << 8) or n - i - 1 >= (0x7FFFFFFF >> 8):
                c.error(loc, "too many expressions in star-unpacking sequence pattern")
            addop_i(c, loc, UNPACK_EX, i + ((n - i - 1) << 8))
            seen_star = True
        elif type(elt) is _ast.MatchStar:
            c.error(loc, "multiple starred expressions in sequence pattern")
    if not seen_star:
        addop_i(c, loc, UNPACK_SEQUENCE, n)


def pattern_helper_sequence_unpack(c, loc, patterns, star, pc):
    codegen_pattern_unpack_helper(c, loc, patterns)
    # We've now got a bunch of new subjects on the stack. They need to remain
    # there after each subpattern match:
    pc.on_top += len(patterns)
    for pattern in patterns:
        # One less item to keep track of each time we loop through:
        pc.on_top -= 1
        codegen_pattern_subpattern(c, pattern, pc)


def pattern_helper_sequence_subscr(c, loc, patterns, star, pc):
    """Like pattern_helper_sequence_unpack, but uses BINARY_OP/NB_SUBSCR
    instead of UNPACK_SEQUENCE / UNPACK_EX. This is more efficient for
    patterns with a starred wildcard like [first, *_] / [first, *_, last] /
    [*_, last] / etc."""
    # We need to keep the subject around for extracting elements:
    pc.on_top += 1
    size = len(patterns)
    for i, pattern in enumerate(patterns):
        if WILDCARD_CHECK(pattern):
            continue
        if i == star:
            assert WILDCARD_STAR_CHECK(pattern)
            continue
        addop_i(c, loc, COPY, 1)
        if i < star:
            addop_load_const(c, loc, i)
        else:
            # The subject may not support negative indexing! Compute a
            # nonnegative index:
            addop(c, loc, GET_LEN)
            addop_load_const(c, loc, size - i)
            addop_binary(c, loc, _ast.Sub(), False)
        addop_i(c, loc, BINARY_OP, NB_SUBSCR)
        codegen_pattern_subpattern(c, pattern, pc)
    # Pop the subject, we're done with it:
    pc.on_top -= 1
    addop(c, loc, POP_TOP)


def codegen_pattern_subpattern(c, p, pc):
    """Like codegen_pattern, but turn off checks for irrefutability."""
    allow_irrefutable = pc.allow_irrefutable
    pc.allow_irrefutable = 1
    codegen_pattern(c, p, pc)
    pc.allow_irrefutable = allow_irrefutable


def codegen_pattern_as(c, p, pc):
    if p.pattern is None:
        # An irrefutable match:
        if not pc.allow_irrefutable:
            if p.name:
                c.error(LOC(p), f"name capture {p.name!r} makes remaining patterns unreachable")
            c.error(LOC(p), "wildcard makes remaining patterns unreachable")
        codegen_pattern_helper_store_name(c, LOC(p), p.name, pc)
        return
    # Need to make a copy for (possibly) storing later:
    pc.on_top += 1
    addop_i(c, LOC(p), COPY, 1)
    codegen_pattern(c, p.pattern, pc)
    # Success! Store it:
    pc.on_top -= 1
    codegen_pattern_helper_store_name(c, LOC(p), p.name, pc)


def codegen_pattern_star(c, p, pc):
    codegen_pattern_helper_store_name(c, LOC(p), p.name, pc)


def validate_kwd_attrs(c, attrs, patterns):
    # Any errors will point to the pattern rather than the arg name as the
    # parser is only supplying identifiers rather than Name or keyword nodes
    for i, attr in enumerate(attrs):
        for j in range(i + 1, len(attrs)):
            if attr == attrs[j]:
                c.error(LOC(patterns[j]), f"attribute name repeated in class pattern: {attr}")


def codegen_pattern_class(c, p, pc):
    patterns = p.patterns
    kwd_attrs = p.kwd_attrs
    kwd_patterns = p.kwd_patterns
    nargs = len(patterns)
    nattrs = len(kwd_attrs)
    nkwd_patterns = len(kwd_patterns)
    if nattrs != nkwd_patterns:
        # AST validator shouldn't let this happen, but if it does, just fail,
        # don't crash out of the interpreter
        c.error(LOC(p), f"kwd_attrs ({nattrs}) / kwd_patterns ({nkwd_patterns}) length "
                        "mismatch in class pattern")
    if nattrs:
        validate_kwd_attrs(c, kwd_attrs, kwd_patterns)
    visit_expr(c, p.cls)
    addop_load_const(c, LOC(p), tuple(kwd_attrs))
    addop_i(c, LOC(p), MATCH_CLASS, nargs)
    addop_i(c, LOC(p), COPY, 1)
    addop_load_const(c, LOC(p), None)
    addop_i(c, LOC(p), IS_OP, 1)
    # TOS is now a tuple of (nargs + nattrs) attributes (or None):
    pc.on_top += 1
    jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    addop_i(c, LOC(p), UNPACK_SEQUENCE, nargs + nattrs)
    pc.on_top += nargs + nattrs - 1
    for pattern in [*patterns, *kwd_patterns]:
        pc.on_top -= 1
        if WILDCARD_CHECK(pattern):
            addop(c, LOC(p), POP_TOP)
            continue
        codegen_pattern_subpattern(c, pattern, pc)
    # Success! Pop the tuple of attributes:


def codegen_pattern_mapping_key(c, seen, p, i):
    key = p.keys[i]
    if key is None:
        c.error(LOC(p.patterns[i]),
                "can't use NULL keys in MatchMapping (set 'rest' parameter instead)")
    if type(key) is _ast.Constant:
        if key.value in seen:
            c.error(LOC(p), f"mapping pattern checks duplicate key ({key.value!r})")
        seen.add(key.value)
    elif type(key) is not _ast.Attribute:
        c.error(LOC(p), "mapping pattern keys may only match literals and attribute lookups")
    visit_expr(c, key)


def codegen_pattern_mapping(c, p, pc):
    keys = p.keys
    patterns = p.patterns
    size = len(keys)
    npatterns = len(patterns)
    if size != npatterns:
        # AST validator shouldn't let this happen, but if it does, just fail,
        # don't crash out of the interpreter
        c.error(LOC(p), f"keys ({size}) / patterns ({npatterns}) length mismatch in mapping pattern")
    # We have a double-star target if "rest" is set
    star_target = p.rest
    # We need to keep the subject on top during the mapping and length checks:
    pc.on_top += 1
    addop(c, LOC(p), MATCH_MAPPING)
    jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    if not size and not star_target:
        # If the pattern is just "{}", we're done! Pop the subject:
        pc.on_top -= 1
        addop(c, LOC(p), POP_TOP)
        return
    if size:
        # If the pattern has any keys in it, perform a length check:
        addop(c, LOC(p), GET_LEN)
        addop_load_const(c, LOC(p), size)
        codegen_addcompare(c, LOC(p), _ast.GtE())
        jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    # Collect all of the keys into a tuple for MATCH_KEYS and **rest. They
    # can either be dotted names or literals.  Maintaining a set of Constant
    # keys allows us to raise a SyntaxError in the case of duplicates.
    seen = set()
    for i in range(size):
        codegen_pattern_mapping_key(c, seen, p, i)
    # all keys have been checked; there are no duplicates
    addop_i(c, LOC(p), BUILD_TUPLE, size)
    addop(c, LOC(p), MATCH_KEYS)
    # There's now a tuple of keys and a tuple of values on top of the subject:
    pc.on_top += 2
    addop_i(c, LOC(p), COPY, 1)
    addop_load_const(c, LOC(p), None)
    addop_i(c, LOC(p), IS_OP, 1)
    jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    # So far so good. Use that tuple of values on the stack to match
    # sub-patterns against:
    addop_i(c, LOC(p), UNPACK_SEQUENCE, size)
    pc.on_top += size - 1
    for pattern in patterns:
        pc.on_top -= 1
        codegen_pattern_subpattern(c, pattern, pc)
    # If we get this far, it's a match! Whatever happens next should consume
    # the tuple of keys and the subject:
    pc.on_top -= 2
    if star_target:
        # If we have a starred name, bind a dict of remaining items to it
        # (this may seem a bit inefficient, but keys is rarely big enough to
        # actually impact runtime):
        # rest = dict(TOS1)
        # for key in TOS:
        #     del rest[key]
        addop_i(c, LOC(p), BUILD_MAP, 0)           # [subject, keys, empty]
        addop_i(c, LOC(p), SWAP, 3)                # [empty, keys, subject]
        addop_i(c, LOC(p), DICT_UPDATE, 2)         # [copy, keys]
        addop_i(c, LOC(p), UNPACK_SEQUENCE, size)  # [copy, keys...]
        while size:
            addop_i(c, LOC(p), COPY, 1 + size)     # [copy, keys..., copy]
            size -= 1
            addop_i(c, LOC(p), SWAP, 2)            # [copy, keys..., copy, key]
            addop(c, LOC(p), DELETE_SUBSCR)        # [copy, keys...]
        codegen_pattern_helper_store_name(c, LOC(p), star_target, pc)
    else:
        addop(c, LOC(p), POP_TOP)  # Tuple of keys.
        addop(c, LOC(p), POP_TOP)  # Subject.


def codegen_pattern_or(c, p, pc):
    end = new_label(c)
    size = len(p.patterns)
    assert size > 1
    # We're going to be messing with pc. Keep the original info handy:
    old_stores, old_allow_irrefutable = pc.stores, pc.allow_irrefutable
    old_fail_pop, old_on_top = pc.fail_pop, pc.on_top
    # control is the list of names bound by the first alternative. It is
    # used for checking different name bindings in alternatives, and for
    # correcting the order in which extracted elements are placed on the
    # stack.
    control = None
    for i, alt in enumerate(p.patterns):
        pc.stores = []
        # An irrefutable sub-pattern must be last, if it is allowed at all:
        pc.allow_irrefutable = (i == size - 1) and old_allow_irrefutable
        pc.fail_pop = []
        pc.on_top = 0
        addop_i(c, LOC(alt), COPY, 1)
        codegen_pattern(c, alt, pc)
        # Success!
        nstores = len(pc.stores)
        if not i:
            # This is the first alternative, so save its stores as a
            # "control" for the others (they can't bind a different set of
            # names, and might need to be reordered):
            control = pc.stores
        elif nstores != len(control):
            c.error(LOC(p), "alternative patterns bind different names")
        elif nstores:
            # There were captures. Check to see if we differ from control:
            for icontrol in reversed(range(nstores)):
                name = control[icontrol]
                if name not in pc.stores:
                    c.error(LOC(p), "alternative patterns bind different names")
                istores = pc.stores.index(name)
                if icontrol != istores:
                    # Reorder the names on the stack to match the order of
                    # the names in control. There's probably a better way of
                    # doing this; the current solution is potentially very
                    # inefficient when each alternative subpattern binds lots
                    # of names in different orders. It's fine for reasonable
                    # cases, though, and the peephole optimizer will ensure
                    # that the final code is as efficient as possible.
                    assert istores < icontrol
                    rotations = istores + 1
                    # Perform the same rotation on pc.stores:
                    rotated = pc.stores[:rotations]
                    del pc.stores[:rotations]
                    pc.stores[icontrol - istores:icontrol - istores] = rotated
                    # Do the same thing to the stack, using several rotations:
                    for _ in range(rotations):
                        codegen_pattern_helper_rotate(c, LOC(alt), icontrol + 1)
        assert control is not None
        addop_j(c, LOC(alt), JUMP, end)
        emit_and_reset_fail_pop(c, LOC(alt), pc)
    pc.stores, pc.allow_irrefutable = old_stores, old_allow_irrefutable
    pc.fail_pop, pc.on_top = old_fail_pop, old_on_top
    # No match. Pop the remaining copy of the subject and fail:
    addop(c, LOC(p), POP_TOP)
    jump_to_fail_pop(c, LOC(p), pc, JUMP)

    use_label(c, end)
    nstores = len(control)
    # There's a bunch of stuff on the stack between where the new stores are
    # and where they need to be:
    # - The other stores.
    # - A copy of the subject.
    # - Anything else that may be on top of the stack.
    # - Any previous stores we've already stashed away on the stack.
    nrots = nstores + 1 + pc.on_top + len(pc.stores)
    for name in control:
        # Rotate this capture to its proper place on the stack:
        codegen_pattern_helper_rotate(c, LOC(p), nrots)
        # Update the list of previous stores with this new name, checking for
        # duplicates:
        if name in pc.stores:
            codegen_error_duplicate_store(c, LOC(p), name)
        pc.stores.append(name)
    # Pop the copy of the subject:
    addop(c, LOC(p), POP_TOP)


def codegen_pattern_sequence(c, p, pc):
    patterns = p.patterns
    size = len(patterns)
    star = -1
    only_wildcard = True
    star_wildcard = False
    # Find a starred name, if it exists. There may be at most one:
    for i, pattern in enumerate(patterns):
        if type(pattern) is _ast.MatchStar:
            if star >= 0:
                c.error(LOC(p), "multiple starred names in sequence pattern")
            star_wildcard = WILDCARD_STAR_CHECK(pattern)
            only_wildcard &= star_wildcard
            star = i
            continue
        only_wildcard &= WILDCARD_CHECK(pattern)
    # We need to keep the subject on top during the sequence and length checks:
    pc.on_top += 1
    addop(c, LOC(p), MATCH_SEQUENCE)
    jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    if star < 0:
        # No star: len(subject) == size
        addop(c, LOC(p), GET_LEN)
        addop_load_const(c, LOC(p), size)
        codegen_addcompare(c, LOC(p), _ast.Eq())
        jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    elif size > 1:
        # Star: len(subject) >= size - 1
        addop(c, LOC(p), GET_LEN)
        addop_load_const(c, LOC(p), size - 1)
        codegen_addcompare(c, LOC(p), _ast.GtE())
        jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)
    # Whatever comes next should consume the subject:
    pc.on_top -= 1
    if only_wildcard:
        # Patterns like: [] / [_] / [_, _] / [*_] / [_, *_] / [_, _, *_] / etc.
        addop(c, LOC(p), POP_TOP)
    elif star_wildcard:
        pattern_helper_sequence_subscr(c, LOC(p), patterns, star, pc)
    else:
        pattern_helper_sequence_unpack(c, LOC(p), patterns, star, pc)


def codegen_pattern_value(c, p, pc):
    value = p.value
    if not MATCH_VALUE_EXPR(value):
        c.error(LOC(p), "patterns may only match literals and attribute lookups")
    visit_expr(c, value)
    codegen_addcompare(c, LOC(p), _ast.Eq())
    addop(c, LOC(p), TO_BOOL)
    jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)


def codegen_pattern_singleton(c, p, pc):
    addop_load_const(c, LOC(p), p.value)
    codegen_addcompare(c, LOC(p), _ast.Is())
    jump_to_fail_pop(c, LOC(p), pc, POP_JUMP_IF_FALSE)


_pattern_visitors = {
    _ast.MatchValue: codegen_pattern_value,
    _ast.MatchSingleton: codegen_pattern_singleton,
    _ast.MatchSequence: codegen_pattern_sequence,
    _ast.MatchMapping: codegen_pattern_mapping,
    _ast.MatchClass: codegen_pattern_class,
    _ast.MatchStar: codegen_pattern_star,
    _ast.MatchAs: codegen_pattern_as,
    _ast.MatchOr: codegen_pattern_or,
}


def codegen_pattern(c, p, pc):
    visitor = _pattern_visitors.get(type(p))
    if visitor is None:
        # AST validator shouldn't let this happen, but if it does, just fail,
        # don't crash out of the interpreter
        c.error(LOC(p), f"invalid match pattern node in AST (kind={type(p).__name__})")
    visitor(c, p, pc)


def codegen_match(c, s):
    pc = PatternContext()
    visit_expr(c, s.subject)
    end = new_label(c)
    cases = len(s.cases)
    assert cases > 0
    m = s.cases[-1]
    has_default = WILDCARD_CHECK(m.pattern) and 1 < cases
    for i in range(cases - has_default):
        m = s.cases[i]
        # Only copy the subject if we're *not* on the last case:
        if i != cases - has_default - 1:
            addop_i(c, LOC(m.pattern), COPY, 1)
        pc.stores = []
        # Irrefutable cases must be either guarded, last, or both:
        pc.allow_irrefutable = m.guard is not None or i == cases - 1
        pc.fail_pop = []
        pc.on_top = 0
        codegen_pattern(c, m.pattern, pc)
        assert not pc.on_top
        # It's a match! Store all of the captured names (they're on the stack).
        for name in pc.stores:
            codegen_nameop(c, LOC(m.pattern), name, Store)
        if m.guard:
            ensure_fail_pop(c, pc, 0)
            codegen_jump_if(c, LOC(m.pattern), m.guard, pc.fail_pop[0], 0)
        # Success! Pop the subject off, we're done with it:
        if i != cases - has_default - 1:
            # Use the next location to give better locations for branch events
            addop(c, NEXT_LOCATION, POP_TOP)
        visit_seq_stmt(c, m.body)
        addop_j(c, NO_LOCATION, JUMP, end)
        # If the pattern fails to match, we want the line number of the
        # cleanup to be associated with the failed pattern, not the last line
        # of the body
        emit_and_reset_fail_pop(c, LOC(m.pattern), pc)
    if has_default:
        # A trailing "case _" is common, and lets us save a bit of redundant
        # pushing and popping in the loop above:
        m = s.cases[-1]
        if cases == 1:
            # No matches. Done with the subject:
            addop(c, LOC(m.pattern), POP_TOP)
        else:
            # Show line coverage for default case (it doesn't create bytecode)
            addop(c, LOC(m.pattern), NOP)
        if m.guard:
            codegen_jump_if(c, LOC(m.pattern), m.guard, end, 0)
        visit_seq_stmt(c, m.body)
    use_label(c, end)


def add_return_at_end(c, add_none):
    """_PyCodegen_AddReturnAtEnd: make sure every instruction stream that
    falls off the end returns None.  This also ensures that no jump target
    offsets are out of bounds."""
    if add_none:
        addop_load_const(c, NO_LOCATION, None)
    addop(c, NO_LOCATION, RETURN_VALUE)
