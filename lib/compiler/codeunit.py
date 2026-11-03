"""The compiler object, its code units and their scopes.

Port of Python/compile.c and Include/internal/pycore_compile.h.  The compiler
makes several passes to build a code object:
  1. Checks for future statements.  See future.py
  2. Builds a symbol table.  See symtable.py.
  3. Generates an instruction sequence.  See compiler_mod() in this file, which
     calls functions from codegen.py.
  4. Generates a control flow graph and runs optimizations on it.  See flowgraph.py.
  5. Assembles the basic blocks into final code.  See
     optimize_and_assemble_code_unit() in this file, and assemble.py.
"""
import sys

import _ast

from . import future, preprocess, symtable
from .consts import merge_consts_recursive
from .instruction_sequence import InstructionSequence

# A soft limit for stack use, to avoid excessive memory use for large
# constants, etc.  The value 30 is plucked out of thin air.
STACK_USE_GUIDELINE = 30

CO_OPTIMIZED = 0x0001
CO_NEWLOCALS = 0x0002
CO_VARARGS = 0x0004
CO_VARKEYWORDS = 0x0008
CO_NESTED = 0x0010
CO_GENERATOR = 0x0020
CO_COROUTINE = 0x0080
CO_ASYNC_GENERATOR = 0x0200
CO_HAS_DOCSTRING = 0x4000000
CO_METHOD = 0x8000000
CO_MAXBLOCKS = 21  # Max static block nesting within a function

PyCF_MASK = (future.CO_FUTURE_DIVISION | future.CO_FUTURE_ABSOLUTE_IMPORT
             | future.CO_FUTURE_WITH_STATEMENT | future.CO_FUTURE_PRINT_FUNCTION
             | future.CO_FUTURE_UNICODE_LITERALS | future.CO_FUTURE_BARRY_AS_BDFL
             | future.CO_FUTURE_GENERATOR_STOP | future.CO_FUTURE_ANNOTATIONS)

# _PyCompile_optype
COMPILE_OP_FAST, COMPILE_OP_GLOBAL, COMPILE_OP_DEREF, COMPILE_OP_NAME = range(4)

# enum _PyCompile_FBlockType
(COMPILE_FBLOCK_WHILE_LOOP,
 COMPILE_FBLOCK_FOR_LOOP,
 COMPILE_FBLOCK_TRY_EXCEPT,
 COMPILE_FBLOCK_FINALLY_TRY,
 COMPILE_FBLOCK_FINALLY_END,
 COMPILE_FBLOCK_WITH,
 COMPILE_FBLOCK_ASYNC_WITH,
 COMPILE_FBLOCK_HANDLER_CLEANUP,
 COMPILE_FBLOCK_POP_VALUE,
 COMPILE_FBLOCK_EXCEPTION_HANDLER,
 COMPILE_FBLOCK_EXCEPTION_GROUP_HANDLER,
 COMPILE_FBLOCK_ASYNC_COMPREHENSION_GENERATOR,
 COMPILE_FBLOCK_INLINED_COMPREHENSION,
 COMPILE_FBLOCK_STOP_ITERATION) = range(14)

(COMPILE_SCOPE_MODULE,
 COMPILE_SCOPE_CLASS,
 COMPILE_SCOPE_FUNCTION,
 COMPILE_SCOPE_ASYNC_FUNCTION,
 COMPILE_SCOPE_LAMBDA,
 COMPILE_SCOPE_COMPREHENSION,
 COMPILE_SCOPE_ANNOTATIONS) = range(7)


class FBlockInfo:
    """_PyCompile_FBlockInfo: a frame block (loop, try/except, try/finally...).

    It's called a frame block to distinguish it from a basic block in the
    compiler IR.
    """
    __slots__ = ("fb_type", "fb_block", "fb_loc", "fb_exit", "fb_datum")

    def __init__(self, fb_type, fb_block, fb_loc, fb_exit, fb_datum):
        self.fb_type = fb_type
        self.fb_block = fb_block
        self.fb_loc = fb_loc
        self.fb_exit = fb_exit          # (optional) type-specific exit or cleanup block
        self.fb_datum = fb_datum        # (optional) additional information required for unwinding


class CodeUnitMetadata:
    """_PyCompile_CodeUnitMetadata.

    u_consts, u_names, u_varnames, u_cellvars and u_freevars are dicts that
    map objects to their index in co_XXX, the oparg of the opcodes that refer
    to those collections.
    """

    def __init__(self, u_argcount=0, u_posonlyargcount=0, u_kwonlyargcount=0):
        self.u_name = None
        self.u_qualname = None      # dot-separated qualified name (lazy)
        self.u_consts = None
        self.u_names = None
        self.u_varnames = None
        self.u_cellvars = None
        self.u_freevars = None
        # keys are names that are fast-locals only temporarily within an
        # inlined comprehension. When value is True, treat as fast-local.
        self.u_fasthidden = None
        self.u_argcount = u_argcount
        self.u_posonlyargcount = u_posonlyargcount
        self.u_kwonlyargcount = u_kwonlyargcount
        self.u_firstlineno = 0


class CompilerUnit:
    """struct compiler_unit: the state that changes on entry and exit of code blocks."""

    def __init__(self, scope_type, umd):
        self.u_ste = None
        self.u_scope_type = scope_type
        self.u_private = None               # for private name mangling
        self.u_static_attributes = None     # for class: attributes accessed via self.X
        self.u_deferred_annotations = None  # AnnAssign nodes deferred to the end of compilation
        # indices of annotations that are conditionally executed (or -1 for
        # unconditional annotations)
        self.u_conditional_annotation_indices = None
        self.u_next_conditional_annotation_index = 0
        self.u_instr_sequence = None            # codegen output
        self.u_stashed_instr_sequence = None    # temporarily stashed parent instruction sequence
        self.u_fblock = []
        self.u_in_inlined_comp = 0
        self.u_in_conditional_block = 0
        self.u_metadata = umd


class InlinedComprehensionState:
    """_PyCompile_InlinedComprehensionState."""

    def __init__(self):
        self.pushed_locals = None
        self.temp_symbols = None
        self.fast_hidden = None
        self.cleanup = None


def dict_add_obj(d, o):
    """_PyCompile_DictAddObj: the index of o in d, adding it if missing."""
    arg = d.get(o)
    if arg is None:
        arg = d[o] = len(d)
    return arg


def list2dict(lst):
    return {k: i for i, k in enumerate(lst)}


def dictbytype(src, scope_type, flag, offset):
    """A dict of the names in symbol dict src that match scope_type or flag.

    The values are integers, starting at offset and increasing by one for
    each key.  The keys are sorted so that the indexes, which index the free
    and cell var storage, and so the bytecode, are deterministic.
    """
    dest = {}
    i = offset
    for k in sorted(src):
        vi = src[k]
        if symtable.SYMBOL_TO_SCOPE(vi) == scope_type or vi & flag:
            dest[k] = i
            i += 1
    return dest


def consts_dict_keys_inorder(d):
    """The constants of d in index order.

    The keys of the dictionary can be tuples wrapping a constant (see
    dict_add_obj and consts.constant_key).  In that case the object we want
    is always second.
    """
    consts = [None] * len(d)
    for k, i in d.items():
        if type(k) is tuple:
            k = k[1]
        consts[i] = k
    return consts


class Compiler:
    """struct _PyCompiler: the global state of a compilation.

    u is the current compilation unit, while units for enclosing blocks are
    stored in c_stack.  enter_scope() and exit_scope() manage both.  Nesting
    levels are not checked here: the symbol analysis pass rejects excessive
    nesting.
    """

    def __init__(self, mod, filename, flags, optimize):
        # compiler_setup
        self.c_const_cache = {}     # all constants, including names tuples
        self.c_stack = []
        self.c_filename = filename
        self.c_future = future.from_ast(mod, filename)
        merged = self.c_future.ff_features | flags
        self.c_future.ff_features = merged
        self.c_flags = merged
        self.c_optimize = sys.flags.optimize if optimize == -1 else optimize
        self.c_interactive = 0
        self.u = None
        # if true, construct recursive instruction sequences (including
        # instructions for nested code objects)
        self.c_save_nested_seqs = False
        self.c_disable_warning = 0
        preprocess.ast_preprocess(mod, filename, self.c_optimize, merged, 0, 1)
        self.c_st = symtable.build(mod, filename, self.c_future)

    def maybe_add_static_attribute_to_class(self, e):
        attr_value = e.value
        if type(attr_value) is not _ast.Name or type(e.ctx) is not _ast.Store or attr_value.id != "self":
            return
        for u in reversed(self.c_stack):
            if u.u_scope_type == COMPILE_SCOPE_CLASS:
                u.u_static_attributes.add(e.attr)
                break

    def set_qualname(self):
        u = self.u
        if u.u_scope_type == COMPILE_SCOPE_MODULE:
            return
        base = None
        stack_size = len(self.c_stack)
        assert stack_size >= 1
        if stack_size > 1:
            force_global = False
            parent = self.c_stack[-1]
            if parent.u_scope_type == COMPILE_SCOPE_ANNOTATIONS:
                # The parent is an annotation scope, so we need to look at
                # the grandparent.
                if stack_size == 2:
                    # If we're immediately within the module, we can skip
                    # the rest and just set the qualname to be the same as name.
                    u.u_metadata.u_qualname = u.u_metadata.u_name
                    return
                parent = self.c_stack[-2]
            if u.u_scope_type in (COMPILE_SCOPE_FUNCTION, COMPILE_SCOPE_ASYNC_FUNCTION,
                                  COMPILE_SCOPE_CLASS):
                mangled = symtable.mangle(parent.u_private, u.u_metadata.u_name)
                scope = symtable.get_scope(parent.u_ste, mangled)
                assert scope != symtable.GLOBAL_IMPLICIT
                if scope == symtable.GLOBAL_EXPLICIT:
                    force_global = True
            if not force_global:
                if parent.u_scope_type in (COMPILE_SCOPE_FUNCTION, COMPILE_SCOPE_ASYNC_FUNCTION,
                                           COMPILE_SCOPE_LAMBDA):
                    base = parent.u_metadata.u_qualname + ".<locals>"
                else:
                    base = parent.u_metadata.u_qualname
        if base is not None:
            u.u_metadata.u_qualname = base + "." + u.u_metadata.u_name
        else:
            u.u_metadata.u_qualname = u.u_metadata.u_name

    def add_const(self, o):
        key = merge_consts_recursive(self.c_const_cache, o)
        return dict_add_obj(self.u.u_metadata.u_consts, key)

    def enter_scope(self, name, scope_type, key, lineno, private, umd):
        u = CompilerUnit(scope_type, umd if umd is not None else CodeUnitMetadata())
        u.u_ste = symtable.lookup(self.c_st, key)
        ste = u.u_ste
        md = u.u_metadata
        md.u_name = name
        md.u_varnames = list2dict(ste.ste_varnames)
        md.u_cellvars = dictbytype(ste.ste_symbols, symtable.CELL, symtable.DEF_COMP_CELL, 0)
        if ste.ste_needs_class_closure:
            # Cook up an implicit __class__ cell.
            assert u.u_scope_type == COMPILE_SCOPE_CLASS
            dict_add_obj(md.u_cellvars, "__class__")
        if ste.ste_needs_classdict:
            # Cook up an implicit __classdict__ cell.
            assert u.u_scope_type == COMPILE_SCOPE_CLASS
            dict_add_obj(md.u_cellvars, "__classdict__")
        if ste.ste_has_conditional_annotations:
            # Cook up an implicit __conditional_annotations__ cell
            assert u.u_scope_type in (COMPILE_SCOPE_CLASS, COMPILE_SCOPE_MODULE)
            dict_add_obj(md.u_cellvars, "__conditional_annotations__")
        md.u_freevars = dictbytype(ste.ste_symbols, symtable.FREE, symtable.DEF_FREE_CLASS,
                                   len(md.u_cellvars))
        md.u_fasthidden = {}
        md.u_firstlineno = lineno
        md.u_consts = {}
        md.u_names = {}
        if scope_type == COMPILE_SCOPE_CLASS:
            u.u_static_attributes = set()
        u.u_instr_sequence = InstructionSequence()
        # Push the old compiler_unit on the stack.
        if self.u is not None:
            self.c_stack.append(self.u)
            if private is None:
                private = self.u.u_private
        u.u_private = private
        self.u = u

    def exit_scope(self):
        nested_seq = self.u.u_instr_sequence if self.c_save_nested_seqs else None
        # Restore c.u to the parent unit.
        if self.c_stack:
            self.u = self.c_stack.pop()
            if nested_seq is not None:
                self.u.u_instr_sequence.add_nested(nested_seq)
        else:
            self.u = None

    # Frame block handling

    def push_fblock(self, loc, t, block_label, exit, datum):
        if len(self.u.u_fblock) >= CO_MAXBLOCKS:
            self.error(loc, "too many statically nested blocks")
        self.u.u_fblock.append(FBlockInfo(t, block_label, loc, exit, datum))
        if t == COMPILE_FBLOCK_FINALLY_END:
            self.c_disable_warning += 1

    def pop_fblock(self, t, block_label):
        f = self.u.u_fblock.pop()
        assert f.fb_type == t and f.fb_block == block_label
        if t == COMPILE_FBLOCK_FINALLY_END:
            self.c_disable_warning -= 1

    def top_fblock(self):
        return self.u.u_fblock[-1] if self.u.u_fblock else None

    def deferred_annotations(self):
        return self.u.u_deferred_annotations, self.u.u_conditional_annotation_indices

    def get_ref_type(self, name):
        if self.u.u_scope_type == COMPILE_SCOPE_CLASS and name in (
                "__class__", "__classdict__", "__conditional_annotations__"):
            return symtable.CELL
        ste = self.u.u_ste
        scope = symtable.get_scope(ste, name)
        if scope == 0:
            raise SystemError(
                f"_PyST_GetScope(name={name!r}) failed: unknown scope in unit "
                f"{self.u.u_metadata.u_name} ({ste.ste_id!r}); symbols: {ste.ste_symbols!r}; "
                f"locals: {self.u.u_metadata.u_varnames!r}; globals: {self.u.u_metadata.u_names!r}")
        return scope

    def lookup_cellvar(self, name):
        return self.u.u_metadata.u_cellvars[name]

    def lookup_arg(self, co, name):
        """The closure index of name for code object co.

        Special case: If a class contains a method with a free variable that
        has the same name as a method, the name will be considered free *and*
        local in the class.  It should be handled by the closure, as well as
        by the normal name lookup logic.
        """
        reftype = self.get_ref_type(name)
        md = self.u.u_metadata
        arg = (md.u_cellvars if reftype == symtable.CELL else md.u_freevars).get(name)
        if arg is None:
            raise SystemError(
                f"compiler_lookup_arg(name={name!r}) with reftype={reftype} failed in "
                f"{md.u_name}; freevars of code {co.co_name}: {co.co_freevars!r}")
        return arg

    def static_attributes_as_tuple(self):
        return tuple(sorted(self.u.u_static_attributes))

    def resolve_nameop(self, mangled, scope):
        """(optype, arg) for a name of the given scope; arg is None for COMPILE_OP_FAST."""
        md = self.u.u_metadata
        d = md.u_names
        optype = COMPILE_OP_NAME
        assert scope >= 0
        if scope == symtable.FREE:
            d = md.u_freevars
            optype = COMPILE_OP_DEREF
        elif scope == symtable.CELL:
            d = md.u_cellvars
            optype = COMPILE_OP_DEREF
        elif scope == symtable.LOCAL:
            if symtable.is_function_like(self.u.u_ste):
                optype = COMPILE_OP_FAST
            elif md.u_fasthidden.get(mangled) is True:
                optype = COMPILE_OP_FAST
        elif scope == symtable.GLOBAL_IMPLICIT:
            if symtable.is_function_like(self.u.u_ste):
                optype = COMPILE_OP_GLOBAL
        elif scope == symtable.GLOBAL_EXPLICIT:
            optype = COMPILE_OP_GLOBAL
        # scope can be 0
        if optype != COMPILE_OP_FAST:
            return optype, dict_add_obj(d, mangled)
        return optype, None

    def tweak_inlined_comprehension_scopes(self, loc, entry, state):
        u = self.u
        in_class_block = u.u_ste.ste_type == symtable.ClassBlock and not u.u_in_inlined_comp
        u.u_in_inlined_comp += 1
        for k, symbol in entry.ste_symbols.items():
            scope = symtable.SYMBOL_TO_SCOPE(symbol)
            outsymbol = symtable.get_symbol(u.u_ste, k)
            outsc = symtable.SYMBOL_TO_SCOPE(outsymbol)
            # If a name has different scope inside than outside the comprehension,
            # we need to temporarily handle it with the right scope while
            # compiling the comprehension. If it's free in the comprehension
            # scope, no special handling; it should be handled the same as the
            # enclosing scope. (If it's free in outer scope and cell in inner
            # scope, we can't treat it as both cell and free in the same function,
            # but treating it as free throughout is fine; it's *_DEREF
            # either way.)
            if ((scope != outsc and scope != symtable.FREE
                 and not (scope == symtable.CELL and outsc == symtable.FREE))
                    or in_class_block):
                if state.temp_symbols is None:
                    state.temp_symbols = {}
                # update the symbol to the in-comprehension version and save
                # the outer version; we'll restore it after running the
                # comprehension
                u.u_ste.ste_symbols[k] = symbol
                state.temp_symbols[k] = outsymbol
            # locals handling for names bound in comprehension (DEF_LOCAL |
            # DEF_NONLOCAL occurs in assignment expression to nonlocal)
            if (symbol & symtable.DEF_LOCAL and not symbol & symtable.DEF_NONLOCAL) or in_class_block:
                if not symtable.is_function_like(u.u_ste):
                    # non-function scope: override this name to use fast locals
                    orig = u.u_metadata.u_fasthidden.get(k)
                    if orig is not True:
                        u.u_metadata.u_fasthidden[k] = True
                        if state.fast_hidden is None:
                            state.fast_hidden = set()
                        state.fast_hidden.add(k)

    def revert_inlined_comprehension_scopes(self, loc, state):
        u = self.u
        u.u_in_inlined_comp -= 1
        if state.temp_symbols:
            u.u_ste.ste_symbols.update(state.temp_symbols)
            state.temp_symbols = None
        if state.fast_hidden:
            while state.fast_hidden:
                k = state.fast_hidden.pop()
                # we set to False instead of clearing, so we can track which names
                # were temporarily fast-locals and should use CO_FAST_HIDDEN
                u.u_metadata.u_fasthidden[k] = False
            state.fast_hidden = None

    def enter_conditional_block(self):
        self.u.u_in_conditional_block += 1

    def leave_conditional_block(self):
        assert self.u.u_in_conditional_block > 0
        self.u.u_in_conditional_block -= 1

    def add_deferred_annotation(self, s):
        """Defer AnnAssign s; its conditional annotation index, or None."""
        u = self.u
        if u.u_deferred_annotations is None:
            u.u_deferred_annotations = []
        if u.u_conditional_annotation_indices is None:
            u.u_conditional_annotation_indices = []
        u.u_deferred_annotations.append(s)
        conditional_annotation_index = None
        if u.u_scope_type == COMPILE_SCOPE_MODULE or u.u_in_conditional_block:
            index = conditional_annotation_index = u.u_next_conditional_annotation_index
            u.u_next_conditional_annotation_index += 1
        else:
            index = -1
        u.u_conditional_annotation_indices.append(index)
        return conditional_annotation_index

    def error(self, loc, msg):
        """Raise a SyntaxError at loc."""
        future.raise_syntax_error(msg, self.c_filename, loc.lineno, loc.col_offset + 1,
                                  loc.end_lineno, loc.end_col_offset + 1)

    def warn(self, loc, msg):
        """Emit a SyntaxWarning; a SyntaxError if the warning is an error."""
        if self.c_disable_warning:
            return
        future.emit_syntax_warning(msg, self.c_filename, loc.lineno, loc.col_offset + 1,
                                   loc.end_lineno, loc.end_col_offset + 1)

    def mangle(self, name):
        return symtable.mangle(self.u.u_private, name)

    def maybe_mangle(self, name):
        return symtable.maybe_mangle(self.u.u_private, self.u.u_ste, name)

    def start_annotation_setup(self):
        assert self.u.u_stashed_instr_sequence is None
        self.u.u_stashed_instr_sequence = self.u.u_instr_sequence
        self.u.u_instr_sequence = InstructionSequence()

    def end_annotation_setup(self):
        parent_seq = self.u.u_stashed_instr_sequence
        assert parent_seq is not None
        anno_seq = self.u.u_instr_sequence
        self.u.u_stashed_instr_sequence = None
        self.u.u_instr_sequence = parent_seq
        parent_seq.set_annotations_code(anno_seq)

    def is_interactive_top_level(self):
        return self.c_interactive and not self.c_stack

    def compute_code_flags(self):
        ste = self.u.u_ste
        flags = 0
        if symtable.is_function_like(ste):
            flags |= CO_NEWLOCALS | CO_OPTIMIZED
            if ste.ste_nested:
                flags |= CO_NESTED
            if ste.ste_generator and not ste.ste_coroutine:
                flags |= CO_GENERATOR
            if ste.ste_generator and ste.ste_coroutine:
                flags |= CO_ASYNC_GENERATOR
            if ste.ste_varargs:
                flags |= CO_VARARGS
            if ste.ste_varkeywords:
                flags |= CO_VARKEYWORDS
            if ste.ste_has_docstring:
                flags |= CO_HAS_DOCSTRING
            if ste.ste_method:
                flags |= CO_METHOD
        if ste.ste_coroutine and not ste.ste_generator:
            flags |= CO_COROUTINE
        # (Only) inherit compilerflags in PyCF_MASK
        flags |= self.c_flags & PyCF_MASK
        return flags

    def optimize_and_assemble(self, add_none):
        """_PyCompile_OptimizeAndAssemble: the code object of the current unit."""
        code_flags = self.compute_code_flags()
        codegen.add_return_at_end(self, add_none)
        return optimize_and_assemble_code_unit(self.u, self.c_const_cache, code_flags,
                                               self.c_filename)


def optimize_and_assemble_code_unit(u, const_cache, code_flags, filename):
    from . import assemble, flowgraph
    consts = consts_dict_keys_inorder(u.u_metadata.u_consts)
    g = flowgraph.cfg_from_instruction_sequence(u.u_instr_sequence)
    nlocals = len(u.u_metadata.u_varnames)
    nparams = len(u.u_ste.ste_varnames)
    assert u.u_metadata.u_firstlineno
    flowgraph.optimize_code_unit(g, consts, const_cache, nlocals, nparams,
                                 u.u_metadata.u_firstlineno)
    optimized_instrs = InstructionSequence()
    stackdepth, nlocalsplus = flowgraph.optimized_cfg_to_instruction_sequence(
        g, u.u_metadata, code_flags, optimized_instrs)
    return assemble.make_code_object(u.u_metadata, const_cache, consts, stackdepth,
                                     optimized_instrs, nlocalsplus, code_flags, filename)


def start_location(stmts):
    """The location of the first statement, so that SETUP_ANNOTATIONS gets
    the line number of the first "real" statement in the module.  If the body
    is empty, the assembler sets the line number later."""
    if stmts:
        st = stmts[0]
        return codegen.LOC(st)
    return codegen.LOCATION(1, 1, 0, 0)


def compiler_codegen(c, mod):
    codegen.enter_anonymous_scope(c, mod)
    assert c.u.u_scope_type == COMPILE_SCOPE_MODULE
    t = type(mod)
    if t is _ast.Module:
        stmts = mod.body
        codegen.codegen_module(c, start_location(stmts), stmts, False)
    elif t is _ast.Interactive:
        c.c_interactive = 1
        stmts = mod.body
        codegen.codegen_module(c, start_location(stmts), stmts, True)
    elif t is _ast.Expression:
        codegen.codegen_expression(c, mod.body)
    else:
        raise SystemError(f"module kind {t.__name__} should not be possible")


def compiler_mod(c, mod):
    add_none = type(mod) is not _ast.Expression
    assert c.u is None
    try:
        compiler_codegen(c, mod)
        return c.optimize_and_assemble(add_none)
    finally:
        if c.u is not None:
            c.exit_scope()


def compile_ast(mod, filename, flags, optimize):
    """_PyAST_Compile: the code object for module mod."""
    c = Compiler(mod, filename, flags, optimize)
    try:
        return compiler_mod(c, mod)
    finally:
        symtable.free(c.c_st)  # compiler_free


def clean_doc(doc):
    """_PyCompile_CleanDoc: inspect.cleandoc() keeping leading and trailing
    blank lines, so that line numbers stay put."""
    doc = doc.expandtabs()
    lines = doc.split("\n")
    # Find minimum indentation of any non-blank lines after first line.
    margin = min((len(line) - len(line.lstrip(" ")) for line in lines[1:]
                  if line.lstrip(" ")), default=0)
    if not doc.startswith(" ") and margin == 0:
        return doc  # doc is already clean.
    out = [lines[0].lstrip(" ")]
    for line in lines[1:]:
        # Strip up to margin spaces; a shorter blank line loses all of them.
        i = 0
        while i < margin and i < len(line) and line[i] == " ":
            i += 1
        out.append(line[i:])
    return "\n".join(out)


def codegen_for_test(mod, filename, flags, optimize, compile_mode=None):
    """_PyCompile_CodeGen: the unoptimized instruction sequence of mod's top
    unit and its metadata, as _testinternalcapi.compiler_codegen returns them."""
    c = Compiler(mod, filename, flags, optimize)
    c.c_save_nested_seqs = True
    try:
        compiler_codegen(c, mod)
        umd = c.u.u_metadata
        metadata = {"argcount": umd.u_argcount, "posonlyargcount": umd.u_posonlyargcount,
                    "kwonlyargcount": umd.u_kwonlyargcount}
        codegen.add_return_at_end(c, type(mod) is not _ast.Expression)
        seq = c.u.u_instr_sequence
        seq.apply_label_map()
        # After add_return_at_end: co_consts indices match the final instruction stream.
        metadata["consts"] = consts_dict_keys_inorder(umd.u_consts)
        return seq, metadata
    finally:
        if c.u is not None:
            c.exit_scope()
        symtable.free(c.c_st)


# codegen.py and this module call each other, as codegen.c and compile.c do;
# codegen imports this module's names, so it is imported last.
from . import codegen  # noqa: E402
