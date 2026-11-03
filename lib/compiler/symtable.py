"""The symbol table (Python/symtable.c, Include/internal/pycore_symtable.h).

Blocks are keyed as in C by the address of the AST object that opens them;
here that is id(node), and the C's "address + 1" keys are id(node) + 1.  The
tree must stay alive while the table is in use.
"""
import _ast

from .future import (LOCATION, PyCF_ALLOW_TOP_LEVEL_AWAIT, PyCF_SOURCE_IS_UTF8, CO_FUTURE_ANNOTATIONS,
                     SourceLocation, from_ast as future_from_ast, get_docstring,
                     ranged_syntax_error, syntax_error_at)

# _Py_block_ty
FunctionBlock = 0
ClassBlock = 1
ModuleBlock = 2
# Used for annotations. If 'from __future__ import annotations' is active,
# annotation blocks cannot bind names and are not evaluated. Otherwise, they
# are lazily evaluated (see PEP 649).
AnnotationBlock = 3
# The following blocks are used for generics and type aliases (PEP 695).
# They work mostly like functions; they differ only for error messages.
TypeAliasBlock = 4
TypeParametersBlock = 5
TypeVariableBlock = 6

# _Py_comprehension_ty
NoComprehension = 0
ListComprehension = 1
DictComprehension = 2
SetComprehension = 3
GeneratorExpression = 4

# Flags for def-use information
DEF_GLOBAL = 1             # global stmt
DEF_LOCAL = 2              # assignment in code block
DEF_PARAM = 2 << 1         # formal parameter
DEF_NONLOCAL = 2 << 2      # nonlocal stmt
USE = 2 << 3               # name is used
DEF_FREE_CLASS = 2 << 5    # free variable from class's method
DEF_IMPORT = 2 << 6        # assignment occurred via import
DEF_ANNOT = 2 << 7         # this name is annotated
DEF_COMP_ITER = 2 << 8     # this name is a comprehension iteration variable
DEF_TYPE_PARAM = 2 << 9    # this name is a type parameter
DEF_COMP_CELL = 2 << 10    # this name is a cell in an inlined comprehension

DEF_BOUND = DEF_LOCAL | DEF_PARAM | DEF_IMPORT

# GLOBAL_EXPLICIT and GLOBAL_IMPLICIT are used internally by the symbol
# table.  GLOBAL is returned from get_scope() for either of them.
# It is stored in ste_symbols at bits 13-16.
SCOPE_OFFSET = 12
SCOPE_MASK = DEF_GLOBAL | DEF_LOCAL | DEF_PARAM | DEF_NONLOCAL


def SYMBOL_TO_SCOPE(s):
    return (s >> SCOPE_OFFSET) & SCOPE_MASK


LOCAL = 1
GLOBAL_EXPLICIT = 2
GLOBAL_IMPLICIT = 3
FREE = 4
CELL = 5

# error strings
GLOBAL_PARAM = "name '%s' is parameter and global"
NONLOCAL_PARAM = "name '%s' is parameter and nonlocal"
GLOBAL_AFTER_ASSIGN = "name '%s' is assigned to before global declaration"
NONLOCAL_AFTER_ASSIGN = "name '%s' is assigned to before nonlocal declaration"
GLOBAL_AFTER_USE = "name '%s' is used prior to global declaration"
NONLOCAL_AFTER_USE = "name '%s' is used prior to nonlocal declaration"
GLOBAL_ANNOT = "annotated name '%s' can't be global"
NONLOCAL_ANNOT = "annotated name '%s' can't be nonlocal"
IMPORT_STAR_WARNING = "import * only allowed at module level"
NAMED_EXPR_COMP_IN_CLASS = "assignment expression within a comprehension cannot be used in a class body"
NAMED_EXPR_COMP_IN_TYPEVAR_BOUND = "assignment expression within a comprehension cannot be used in a TypeVar bound"
NAMED_EXPR_COMP_IN_TYPEALIAS = "assignment expression within a comprehension cannot be used in a type alias"
NAMED_EXPR_COMP_IN_TYPEPARAM = ("assignment expression within a comprehension cannot be used within the "
                                "definition of a generic")
NAMED_EXPR_COMP_CONFLICT = "assignment expression cannot rebind comprehension iteration variable '%s'"
NAMED_EXPR_COMP_INNER_LOOP_CONFLICT = "comprehension inner loop cannot rebind assignment expression target '%s'"
NAMED_EXPR_COMP_ITER_EXPR = "assignment expression cannot be used in a comprehension iterable expression"
ANNOTATION_NOT_ALLOWED = "%s cannot be used within an annotation"
EXPR_NOT_ALLOWED_IN_TYPE_VARIABLE = "%s cannot be used within %s"
EXPR_NOT_ALLOWED_IN_TYPE_ALIAS = "%s cannot be used within a type alias"
EXPR_NOT_ALLOWED_IN_TYPE_PARAMETERS = "%s cannot be used within the definition of a generic"
DUPLICATE_TYPE_PARAM = "duplicate type parameter '%s'"
ASYNC_WITH_OUTSIDE_ASYNC_FUNC = "'async with' outside async function"
ASYNC_FOR_OUTSIDE_ASYNC_FUNC = "'async for' outside async function"
DUPLICATE_ARGUMENT = "duplicate argument '%s' in function definition"


class SymTableEntry:
    """PySTEntryObject; also exposes the attributes of _symtable's entries."""

    def __init__(self):
        self.ste_id = None          # int: key in ste_table.st_blocks
        self.ste_symbols = {}       # dict: variable names to flags
        self.ste_name = None        # string: name of current block
        self.ste_varnames = []      # list of function parameters
        self.ste_children = []      # list of child blocks
        self.ste_directives = None  # locations of global and nonlocal statements
        self.ste_mangled_names = None  # set of names for which mangling should be applied
        self.ste_type = None
        # Optional description of the current "context" for error messages,
        # e.g. "a TypeVar default".
        self.ste_scope_info = None
        self.ste_nested = 0         # true if block is nested
        self.ste_generator = 0      # true if namespace is a generator
        self.ste_coroutine = 0      # true if namespace is a coroutine
        self.ste_annotations_used = 0  # true if there are any annotations in this scope
        self.ste_comprehension = NoComprehension
        self.ste_varargs = 0        # true if block has varargs
        self.ste_varkeywords = 0    # true if block has varkeywords
        self.ste_returns_value = 0  # true if namespace uses return with an argument
        self.ste_needs_class_closure = 0  # for class scopes, true if a closure over __class__ is needed
        self.ste_needs_classdict = 0  # for class scopes, true if a closure over the class dict is needed
        self.ste_comp_inlined = 0   # true if this comprehension is inlined
        self.ste_comp_iter_target = 0  # true if visiting comprehension target
        self.ste_can_see_class_scope = 0  # true if this block can see names bound in an enclosing class
        self.ste_has_docstring = 0  # true if docstring present
        self.ste_method = 0         # true if block is a function block defined in class scope
        self.ste_has_conditional_annotations = 0  # true if block has conditionally executed annotations
        self.ste_in_conditional_block = 0  # set while inside a conditionally executed block
        self.ste_in_unevaluated_annotation = 0  # set while processing an annotation that is not evaluated
        self.ste_comp_iter_expr = 0  # non-zero if visiting a comprehension range expression
        self.ste_loc = None         # source location of block
        self.ste_annotation_block = None  # symbol table entry for this entry's annotations
        self.ste_table = None

    # The members _symtable exposes (ste_memberlist).
    id = property(lambda self: self.ste_id)
    name = property(lambda self: self.ste_name)
    symbols = property(lambda self: self.ste_symbols)
    varnames = property(lambda self: self.ste_varnames)
    children = property(lambda self: self.ste_children)
    nested = property(lambda self: self.ste_nested)
    type = property(lambda self: self.ste_type)
    lineno = property(lambda self: self.ste_loc.lineno)

    def __repr__(self):
        return f"<symtable entry {self.ste_name}({self.ste_id!r}), line {self.ste_loc.lineno}>"


class SymTable:
    """struct symtable."""

    def __init__(self):
        self.st_filename = None     # name of file being compiled
        self.st_cur = None          # current symbol table entry
        self.st_top = None          # symbol table entry for module
        self.st_blocks = {}         # map AST node addresses to symbol table entries
        self.st_stack = []          # stack of namespace info
        self.st_global = None       # st_top.ste_symbols
        self.st_private = None      # name of current class or None
        self.st_future = None       # module's future features that affect the symbol table


def ste_new(st, name, block, key, loc):
    ste = SymTableEntry()
    ste.ste_table = st
    ste.ste_id = key
    ste.ste_name = name
    ste.ste_type = block
    ste.ste_loc = loc
    if st.st_cur is not None and (st.st_cur.ste_nested or is_function_like(st.st_cur)):
        ste.ste_nested = 1
    if st.st_cur is not None and st.st_cur.ste_type == ClassBlock and block == FunctionBlock:
        ste.ste_method = 1
    st.st_blocks[ste.ste_id] = ste
    return ste


def build(mod, filename, future):
    """_PySymtable_Build: the SymTable of mod, or SyntaxError."""
    st = SymTable()
    st.st_filename = filename
    st.st_future = future

    # Make the initial symbol information gathering pass
    symtable_enter_block(st, "top", ModuleBlock, id(mod), SourceLocation(0, 0, 0, 0))
    st.st_top = st.st_cur
    if isinstance(mod, _ast.Module):
        seq = mod.body
        if get_docstring(seq) is not None:
            st.st_cur.ste_has_docstring = 1
        for s in seq:
            symtable_visit_stmt(st, s)
    elif isinstance(mod, _ast.Expression):
        symtable_visit_expr(st, mod.body)
    elif isinstance(mod, _ast.Interactive):
        for s in mod.body:
            symtable_visit_stmt(st, s)
    elif isinstance(mod, _ast.FunctionType):
        raise RuntimeError("this compiler does not handle FunctionTypes")
    symtable_exit_block(st)
    # Make the second symbol analysis pass
    symtable_analyze(st)
    return st


def lookup(st, key):
    """_PySymtable_Lookup: the entry for key (id(node), or id(node) + 1)."""
    try:
        return st.st_blocks[key]
    except KeyError:
        raise KeyError("unknown symbol table entry") from None


def lookup_optional(st, key):
    """_PySymtable_LookupOptional: the entry for key, or None."""
    return st.st_blocks.get(key)


def get_symbol(ste, name):
    """_PyST_GetSymbol."""
    return ste.ste_symbols.get(name, 0)


def get_scope(ste, name):
    """_PyST_GetScope."""
    return SYMBOL_TO_SCOPE(get_symbol(ste, name))


def is_function_like(ste):
    """_PyST_IsFunctionLike."""
    return ste.ste_type in (FunctionBlock, AnnotationBlock, TypeVariableBlock, TypeAliasBlock,
                            TypeParametersBlock)


def error_at_directive(ste, name, msg):
    for data in ste.ste_directives:
        if data[0] == name:
            return ranged_syntax_error(msg, ste.ste_table.st_filename, data[1], data[2] + 1,
                                       data[3], data[4] + 1)
    return RuntimeError("BUG: internal directive bookkeeping broken")


# Analyze raw symbol information to determine scope of each name.
#
# The next several functions are helpers for symtable_analyze(), which
# determines whether a name is local, global, or free.  In addition, it
# determines which local variables are cell variables; they provide bindings
# that are used for free variables in enclosed blocks.
#
# There are also two kinds of global variables, implicit and explicit.  An
# explicit global is declared with the global statement.  An implicit global
# is a free variable for which the compiler has found no binding in an
# enclosing function scope.  The implicit global is either a global or a
# builtin.  Python's module and class blocks use the xxx_NAME opcodes to
# handle these names to implement slightly odd semantics.  In such a block,
# the name is treated as global until it is assigned to; then it is treated
# as a local.
#
# The symbol table requires two passes to determine the scope of each name.
# The first pass collects raw facts from the AST via the symtable_visit_*
# functions: the name is a parameter here, the name is used but not defined
# here, etc.  The second pass analyzes these facts during a pass over the
# entries created during pass 1.
#
# When a function is entered during the second pass, the parent passes the
# set of all name bindings visible to its children.  These bindings are used
# to determine if non-local variables are free or implicit globals.  Names
# which are explicitly declared nonlocal must exist in this set of visible
# names - if they do not, a syntax error is raised.  After doing the local
# analysis, it analyzes each of its child blocks using an updated set of name
# bindings.
#
# The children update the free variable set.  If a local variable is added to
# the free variable set by the child, the variable is marked as a cell.  The
# function object being defined must provide runtime storage for the
# variable that may outlive the function's frame.  Cell variables are removed
# from the free set before the analyze function returns to its parent.
#
# During analysis, the names are:
#    symbols: dict mapping from symbol names to flag values (including offset scope values)
#    scopes: dict mapping from symbol names to scope values (no offset)
#    local: set of all symbol names local to the current scope
#    bound: set of all symbol names local to a containing function scope
#    free: set of all symbol names referenced but not bound in child scopes
#    global: set of all symbol names explicitly declared as global

def analyze_name(ste, scopes, name, flags, bound, local, free, global_, type_params, class_entry):
    """Decide on scope of name, given flags.

    The namespace dictionaries may be modified to record information about
    the new name.  For example, a new global will add an entry to global.  A
    name that was global can be changed to local."""
    if flags & DEF_GLOBAL:
        if flags & DEF_NONLOCAL:
            raise error_at_directive(ste, name, f"name '{name}' is nonlocal and global")
        scopes[name] = GLOBAL_EXPLICIT
        global_.add(name)
        if bound is not None:
            bound.discard(name)
        return
    if flags & DEF_NONLOCAL:
        if bound is None:
            raise error_at_directive(ste, name, "nonlocal declaration not allowed at module level")
        if name not in bound:
            raise error_at_directive(ste, name, f"no binding for nonlocal '{name}' found")
        if name in type_params:
            raise error_at_directive(ste, name, f"nonlocal binding not allowed for type parameter '{name}'")
        scopes[name] = FREE
        free.add(name)
        return
    if flags & DEF_BOUND:
        scopes[name] = LOCAL
        local.add(name)
        global_.discard(name)
        if flags & DEF_TYPE_PARAM:
            type_params.add(name)
        else:
            type_params.discard(name)
        return
    # If we were passed class_entry (i.e., we're in an ste_can_see_class_scope
    # scope) and the bound name is in that set, then the name is potentially
    # bound both by the immediately enclosing class namespace, and also by an
    # outer function namespace.  In that case, we want the runtime name
    # resolution to look at only the class namespace and the globals (not the
    # namespace providing the bound).  Similarly, if the name is explicitly
    # global in the class namespace (through the global statement), we want
    # to also treat it as a global in this scope.
    if class_entry is not None:
        class_flags = get_symbol(class_entry, name)
        if class_flags & DEF_GLOBAL:
            scopes[name] = GLOBAL_EXPLICIT
            return
        elif (class_flags & DEF_BOUND) and not (class_flags & DEF_NONLOCAL):
            scopes[name] = GLOBAL_IMPLICIT
            return
    # If an enclosing block has a binding for this name, it is a free
    # variable rather than a global variable.  Note that having a non-None
    # bound implies that the block is nested.
    if bound is not None and name in bound:
        scopes[name] = FREE
        free.add(name)
        return
    # If a parent has a global statement, then call it global explicit?  It
    # could also be global implicit.
    if global_ is not None and name in global_:
        scopes[name] = GLOBAL_IMPLICIT
        return
    scopes[name] = GLOBAL_IMPLICIT


def is_free_in_any_child(entry, key):
    for child_ste in entry.ste_children:
        if get_scope(child_ste, key) == FREE:
            return True
    return False


def inline_comprehension(ste, comp, scopes, comp_free, inlined_cells):
    remove_dunder_class = False
    remove_dunder_classdict = False
    remove_dunder_cond_annotations = False
    for k, comp_flags in comp.ste_symbols.items():
        # skip comprehension parameter
        if comp_flags & DEF_PARAM:
            assert k == ".0"
            continue
        scope = SYMBOL_TO_SCOPE(comp_flags)
        only_flags = comp_flags & ((1 << SCOPE_OFFSET) - 1)
        if scope == CELL or only_flags & DEF_COMP_CELL:
            inlined_cells.add(k)
        existing = ste.ste_symbols.get(k)
        # __class__, __classdict__ and __conditional_annotations__ are not
        # allowed to be free through a class scope (see drop_class_free)
        # unless children scopes need it
        if (scope == FREE and ste.ste_type == ClassBlock
                and k in ("__class__", "__classdict__", "__conditional_annotations__")):
            scope = GLOBAL_IMPLICIT
            if not is_free_in_any_child(comp, k):
                comp_free.discard(k)
            if k == "__class__":
                remove_dunder_class = True
            elif k == "__conditional_annotations__":
                remove_dunder_cond_annotations = True
            else:
                remove_dunder_classdict = True
        if existing is None:
            # name does not exist in scope, copy from comprehension
            assert scope != FREE or k in comp_free
            ste.ste_symbols[k] = only_flags
            scopes[k] = scope
        else:
            flags = existing
            # Propagate uses of enclosing bindings, but not class locals,
            # which are not visible to comprehensions.
            if scope == FREE and ste.ste_type != ClassBlock and (comp_flags & USE) and not (flags & USE):
                ste.ste_symbols[k] = flags | USE
            if (flags & DEF_BOUND) and ste.ste_type != ClassBlock:
                # free vars in comprehension that are locals in outer scope
                # can now simply be locals, unless they are free in comp
                # children, or if the outer scope is a class block
                if not is_free_in_any_child(comp, k):
                    comp_free.discard(k)
    if remove_dunder_class:
        del comp.ste_symbols["__class__"]
    if remove_dunder_classdict:
        del comp.ste_symbols["__classdict__"]
    if remove_dunder_cond_annotations:
        del comp.ste_symbols["__conditional_annotations__"]


def analyze_cells(scopes, free, inlined_cells):
    """If a name is defined in free and also in locals, then this block
    provides the binding for the free variable.  The name should be marked
    CELL in this block and removed from the free list.

    Note that the current block's free variables are included in free.
    That's safe because no name can be free and local in the same scope."""
    for name, scope in scopes.items():
        if scope != LOCAL:
            continue
        if name not in free and name not in inlined_cells:
            continue
        # Replace LOCAL with CELL for this name, and remove from free.
        scopes[name] = CELL
        free.discard(name)


def drop_class_free(ste, free):
    if "__class__" in free:
        free.discard("__class__")
        ste.ste_needs_class_closure = 1
    if "__classdict__" in free:
        free.discard("__classdict__")
        ste.ste_needs_classdict = 1
    if "__conditional_annotations__" in free:
        free.discard("__conditional_annotations__")
        ste.ste_has_conditional_annotations = 1


def update_symbols(symbols, scopes, bound, free, inlined_cells, classflag):
    """Enter the final scope information into the ste_symbols dict."""
    # Update scope information for all symbols in this scope
    for name, flags in symbols.items():
        if name in inlined_cells:
            flags |= DEF_COMP_CELL
        symbols[name] = flags | (scopes[name] << SCOPE_OFFSET)

    # Record not yet resolved free variables from children (if any)
    v_free = FREE << SCOPE_OFFSET
    for name in free:
        v = symbols.get(name)
        # Handle symbol that already exists in this scope
        if v is not None:
            # Handle a free variable in a method of the class that has the
            # same name as a local or global in the class scope.
            if classflag:
                symbols[name] = v | DEF_FREE_CLASS
            # It's a cell, or already free in this scope
            continue
        # Handle global symbol
        if bound is not None and name not in bound:
            continue        # it's a global
        # Propagate new free symbol up the lexical stack
        symbols[name] = v_free


def analyze_block(ste, bound, free, global_, type_params, class_entry):
    """Make final symbol table decisions for block of ste.

    ste -- current symtable entry (input/output)
    bound -- set of variables bound in enclosing scopes (input).  bound is
        None for module blocks.
    free -- set of free variables in enclosed scopes (output)
    globals -- set of declared global variables in enclosing scopes (input)

    analyze_block() analyzes the individual names defined in a block;
    analyze_child_block() prepares temporary namespace sets used to
    evaluate nested blocks, because a child block should see the name
    bindings of its enclosing blocks, but those bindings should not
    propagate back to a parent block."""
    local = set()       # collect new names bound in block
    scopes = {}         # collect scopes defined for each name

    # Allocate new global, bound and free variable sets.  These sets hold the
    # names visible in nested blocks.  For ClassBlocks, the bound and global
    # names are initialized before analyzing names, because class bindings
    # aren't visible in methods.  For other blocks, they are initialized
    # after names are analyzed.
    newglobal = set()
    newfree = set()
    newbound = set()
    inlined_cells = set()

    # Class namespace has no effect on names visible in nested functions, so
    # populate the global and bound sets to be passed to child blocks before
    # analyzing this one.
    if ste.ste_type == ClassBlock:
        # Pass down known globals
        newglobal |= global_
        # Pass down previously bound symbols
        if bound is not None:
            newbound |= bound

    for name, flags in ste.ste_symbols.items():
        analyze_name(ste, scopes, name, flags, bound, local, free, global_, type_params, class_entry)

    # Populate global and bound sets to be passed to children.
    if ste.ste_type != ClassBlock:
        # Add function locals to bound set
        if is_function_like(ste):
            newbound |= local
        # Pass down previously bound symbols
        if bound is not None:
            newbound |= bound
        # Pass down known globals
        newglobal |= global_
    else:
        # Special-case __class__ and __classdict__
        newbound.add("__class__")
        newbound.add("__classdict__")
        newbound.add("__conditional_annotations__")

    # Recursively call analyze_child_block() on each child block.
    #
    # newbound, newglobal now contain the names visible in nested blocks.
    # The free variables in the children will be added to newfree.
    for entry in ste.ste_children:
        new_class_entry = None
        if entry.ste_can_see_class_scope:
            if ste.ste_type == ClassBlock:
                new_class_entry = ste
            elif class_entry is not None:
                new_class_entry = class_entry

        # we inline all non-generator-expression comprehensions, except
        # those in annotation scopes that are nested in classes
        inline_comp = entry.ste_comprehension and not entry.ste_generator and not ste.ste_can_see_class_scope

        child_free = analyze_child_block(entry, newbound, newfree, newglobal, type_params, new_class_entry)
        if inline_comp:
            inline_comprehension(ste, entry, scopes, child_free, inlined_cells)
            entry.ste_comp_inlined = 1
        newfree |= child_free

    # Splice children of inlined comprehensions into our children list
    children = ste.ste_children
    for i in range(len(children) - 1, -1, -1):
        entry = children[i]
        if entry.ste_comp_inlined:
            children[i:i + 1] = entry.ste_children

    # Check if any local variables must be converted to cell variables
    if is_function_like(ste):
        analyze_cells(scopes, newfree, inlined_cells)
    elif ste.ste_type == ClassBlock:
        drop_class_free(ste, newfree)
    # Records the results of the analysis in the symbol table entry
    update_symbols(ste.ste_symbols, scopes, bound, newfree, inlined_cells,
                   ste.ste_type == ClassBlock or ste.ste_can_see_class_scope)

    free |= newfree


def analyze_child_block(entry, bound, free, global_, type_params, class_entry):
    """The free set of entry, analyzed with copies of the enclosing sets."""
    # Copy the bound/global/free sets.  These sets are used by all blocks
    # enclosed by the current block.  The analyze_block() call modifies
    # these sets.
    temp_bound = set(bound)
    temp_free = set(free)
    temp_global = set(global_)
    temp_type_params = set(type_params)
    analyze_block(entry, temp_bound, temp_free, temp_global, temp_type_params, class_entry)
    return temp_free


def symtable_analyze(st):
    analyze_block(st.st_top, None, set(), set(), set(), None)


def symtable_exit_block(st):
    st.st_cur = None
    if st.st_stack:
        st.st_stack.pop()
        if st.st_stack:
            st.st_cur = st.st_stack[-1]


def symtable_enter_existing_block(st, ste, add_to_children):
    st.st_stack.append(ste)
    prev = st.st_cur
    # bpo-37757: For now, disallow *all* assignment expressions in the
    # outermost iterator expression of a comprehension, even those inside a
    # nested comprehension or a lambda expression.
    if prev is not None:
        ste.ste_comp_iter_expr = prev.ste_comp_iter_expr
    # No need to inherit ste_mangled_names in classes, where all names are
    # mangled.
    if prev is not None and prev.ste_mangled_names is not None and ste.ste_type != ClassBlock:
        ste.ste_mangled_names = prev.ste_mangled_names
    st.st_cur = ste

    # If "from __future__ import annotations" is active, annotation blocks
    # shouldn't have any affect on the symbol table since in the compilation
    # stage, they will all be transformed to strings.
    if st.st_future.ff_features & CO_FUTURE_ANNOTATIONS and ste.ste_type == AnnotationBlock:
        return

    if ste.ste_type == ModuleBlock:
        st.st_global = st.st_cur.ste_symbols

    if add_to_children and prev is not None:
        prev.ste_children.append(ste)


def symtable_enter_block(st, name, block, key, loc):
    ste = ste_new(st, name, block, key, loc)
    symtable_enter_existing_block(st, ste, True)
    if block in (AnnotationBlock, TypeVariableBlock, TypeAliasBlock):
        # We need to insert code that reads this "parameter" to the function.
        symtable_add_def(st, ".format", DEF_PARAM, loc)
        symtable_add_def(st, ".format", USE, loc)


def symtable_lookup_entry(st, ste, name):
    return get_symbol(ste, maybe_mangle(st.st_private, ste, name))


def symtable_lookup(st, name):
    return symtable_lookup_entry(st, st.st_cur, name)


def symtable_add_def_helper(st, name, flag, ste, loc):
    mangled = maybe_mangle(st.st_private, st.st_cur, name)
    d = ste.ste_symbols
    val = d.get(mangled)
    if val is not None:
        if (flag & DEF_PARAM) and (val & DEF_PARAM):
            # Is it better to use 'mangled' or 'name' here?
            raise syntax_error_at(DUPLICATE_ARGUMENT % name, st.st_filename, loc)
        if (flag & DEF_TYPE_PARAM) and (val & DEF_TYPE_PARAM):
            raise syntax_error_at(DUPLICATE_TYPE_PARAM % name, st.st_filename, loc)
        val |= flag
    else:
        val = flag
    if ste.ste_comp_iter_target:
        # This name is an iteration variable in a comprehension, so check for
        # a binding conflict with any named expressions.  Otherwise, mark it
        # as an iteration variable so subsequent named expressions can check
        # for conflicts.
        if val & (DEF_GLOBAL | DEF_NONLOCAL):
            raise syntax_error_at(NAMED_EXPR_COMP_INNER_LOOP_CONFLICT % name, st.st_filename, loc)
        val |= DEF_COMP_ITER
    d[mangled] = val

    if flag & DEF_PARAM:
        ste.ste_varnames.append(mangled)
    elif flag & DEF_GLOBAL:
        # XXX need to update DEF_GLOBAL for other flags too;
        # perhaps only DEF_FREE_GLOBAL
        st.st_global[mangled] = st.st_global.get(mangled, 0) | flag


def check_name(st, name, loc, ctx):
    if ctx is _ast.Store and name == "__debug__":
        raise syntax_error_at("cannot assign to __debug__", st.st_filename, loc)
    if ctx is _ast.Del and name == "__debug__":
        raise syntax_error_at("cannot delete __debug__", st.st_filename, loc)


def check_keywords(st, keywords):
    for key in keywords:
        if key.arg:
            check_name(st, key.arg, LOCATION(key), _ast.Store)


def check_kwd_patterns(st, p):
    for attr, pattern in zip(p.kwd_attrs, p.kwd_patterns):
        check_name(st, attr, LOCATION(pattern), _ast.Store)


def symtable_add_def_ctx(st, name, flag, loc, ctx):
    write_mask = DEF_PARAM | DEF_LOCAL | DEF_IMPORT
    if flag & write_mask:
        check_name(st, name, loc, ctx)
    if (flag & DEF_TYPE_PARAM) and st.st_cur.ste_mangled_names is not None:
        st.st_cur.ste_mangled_names.add(name)
    symtable_add_def_helper(st, name, flag, st.st_cur, loc)


def symtable_add_def(st, name, flag, loc):
    symtable_add_def_ctx(st, name, flag, loc, _ast.Load if flag == USE else _ast.Store)


def symtable_enter_type_param_block(st, name, key, has_defaults, has_kwdefaults, kind, loc):
    current_type = st.st_cur.ste_type
    symtable_enter_block(st, name, TypeParametersBlock, key, loc)
    if current_type == ClassBlock:
        st.st_cur.ste_can_see_class_scope = 1
        symtable_add_def(st, "__classdict__", USE, loc)
    if kind is _ast.ClassDef:
        # It gets "set" when we create the type params tuple and "used" when
        # we build up the bases.
        symtable_add_def(st, ".type_params", DEF_LOCAL, loc)
        symtable_add_def(st, ".type_params", USE, loc)
        # This is used for setting the generic base
        symtable_add_def(st, ".generic_base", DEF_LOCAL, loc)
        symtable_add_def(st, ".generic_base", USE, loc)
    if has_defaults:
        symtable_add_def(st, ".defaults", DEF_PARAM, loc)
    if has_kwdefaults:
        symtable_add_def(st, ".kwdefaults", DEF_PARAM, loc)


def symtable_record_directive(st, name, loc):
    if st.st_cur.ste_directives is None:
        st.st_cur.ste_directives = []
    mangled = maybe_mangle(st.st_private, st.st_cur, name)
    st.st_cur.ste_directives.append((mangled, loc.lineno, loc.col_offset, loc.end_lineno, loc.end_col_offset))


def has_kwonlydefaults(kwonlyargs, kw_defaults):
    return any(kw_defaults[i] is not None for i in range(len(kwonlyargs)))


def check_import_from(st, s):
    fut = st.st_future.ff_location
    if (s.module and s.level == 0 and s.module == "__future__"
            and (s.lineno > fut.lineno
                 or (s.lineno == fut.end_lineno and s.col_offset > fut.end_col_offset))):
        raise syntax_error_at("from __future__ imports must occur at the beginning of the file",
                              st.st_filename, LOCATION(s))


def allows_top_level_await(st):
    return bool(st.st_future.ff_features & PyCF_ALLOW_TOP_LEVEL_AWAIT) and st.st_cur.ste_type == ModuleBlock


def maybe_set_ste_coroutine_for_module(st, s):
    if allows_top_level_await(st):
        st.st_cur.ste_coroutine = 1


def IS_ASYNC_DEF(st):
    return st.st_cur.ste_type == FunctionBlock and st.st_cur.ste_coroutine


class ConditionalBlock:
    """ENTER_CONDITIONAL_BLOCK / LEAVE_CONDITIONAL_BLOCK."""

    def __init__(self, st):
        self.ste = st.st_cur

    def __enter__(self):
        self.saved = self.ste.ste_in_conditional_block
        self.ste.ste_in_conditional_block = 1

    def __exit__(self, *exc):
        self.ste.ste_in_conditional_block = self.saved


def visit_function_def(st, s, is_async):
    args = s.args
    symtable_add_def(st, s.name, DEF_LOCAL, LOCATION(s))
    for e in args.defaults:
        symtable_visit_expr(st, e)
    for e in args.kw_defaults:
        if e is not None:
            symtable_visit_expr(st, e)
    for e in s.decorator_list:
        symtable_visit_expr(st, e)
    if s.type_params:
        # The C tests the defaults sequence for NULL, which it never is.
        symtable_enter_type_param_block(st, s.name, id(s.type_params), args.defaults is not None,
                                        has_kwonlydefaults(args.kwonlyargs, args.kw_defaults),
                                        type(s), LOCATION(s))
        for tp in s.type_params:
            symtable_visit_type_param(st, tp)
    new_ste = ste_new(st, s.name, FunctionBlock, id(s), LOCATION(s))
    if get_docstring(s.body) is not None:
        new_ste.ste_has_docstring = 1
    symtable_visit_annotations(st, s, args, s.returns, new_ste)
    symtable_enter_existing_block(st, new_ste, True)
    if is_async:
        st.st_cur.ste_coroutine = 1
    symtable_visit_arguments(st, args)
    for stmt in s.body:
        symtable_visit_stmt(st, stmt)
    symtable_exit_block(st)
    if s.type_params:
        symtable_exit_block(st)


def symtable_visit_stmt(st, s):
    if isinstance(s, _ast.FunctionDef):
        visit_function_def(st, s, False)
    elif isinstance(s, _ast.ClassDef):
        symtable_add_def(st, s.name, DEF_LOCAL, LOCATION(s))
        for e in s.decorator_list:
            symtable_visit_expr(st, e)
        tmp = st.st_private
        if s.type_params:
            symtable_enter_type_param_block(st, s.name, id(s.type_params), False, False, _ast.ClassDef,
                                            LOCATION(s))
            st.st_private = s.name
            st.st_cur.ste_mangled_names = set()
            for tp in s.type_params:
                symtable_visit_type_param(st, tp)
        for e in s.bases:
            symtable_visit_expr(st, e)
        check_keywords(st, s.keywords)
        for k in s.keywords:
            symtable_visit_keyword(st, k)
        symtable_enter_block(st, s.name, ClassBlock, id(s), LOCATION(s))
        st.st_private = s.name
        if s.type_params:
            symtable_add_def(st, "__type_params__", DEF_LOCAL, LOCATION(s))
            symtable_add_def(st, ".type_params", USE, LOCATION(s))
        if get_docstring(s.body) is not None:
            st.st_cur.ste_has_docstring = 1
        for stmt in s.body:
            symtable_visit_stmt(st, stmt)
        symtable_exit_block(st)
        if s.type_params:
            symtable_exit_block(st)
        st.st_private = tmp
    elif isinstance(s, _ast.TypeAlias):
        symtable_visit_expr(st, s.name)
        assert isinstance(s.name, _ast.Name)
        name = s.name.id
        is_in_class = st.st_cur.ste_type == ClassBlock
        is_generic = len(s.type_params) > 0
        if is_generic:
            symtable_enter_type_param_block(st, name, id(s.type_params), False, False, _ast.TypeAlias,
                                            LOCATION(s))
            for tp in s.type_params:
                symtable_visit_type_param(st, tp)
        symtable_enter_block(st, name, TypeAliasBlock, id(s), LOCATION(s))
        st.st_cur.ste_can_see_class_scope = int(is_in_class)
        if is_in_class:
            symtable_add_def(st, "__classdict__", USE, LOCATION(s.value))
        symtable_visit_expr(st, s.value)
        symtable_exit_block(st)
        if is_generic:
            symtable_exit_block(st)
    elif isinstance(s, _ast.Return):
        if s.value is not None:
            symtable_visit_expr(st, s.value)
            st.st_cur.ste_returns_value = 1
    elif isinstance(s, _ast.Delete):
        for e in s.targets:
            symtable_visit_expr(st, e)
    elif isinstance(s, _ast.Assign):
        for e in s.targets:
            symtable_visit_expr(st, e)
        symtable_visit_expr(st, s.value)
    elif isinstance(s, _ast.AnnAssign):
        st.st_cur.ste_annotations_used = 1
        if isinstance(s.target, _ast.Name):
            e_name = s.target
            cur = symtable_lookup(st, e_name.id)
            if (cur & (DEF_GLOBAL | DEF_NONLOCAL)) and st.st_cur.ste_symbols is not st.st_global and s.simple:
                msg = GLOBAL_ANNOT if cur & DEF_GLOBAL else NONLOCAL_ANNOT
                raise syntax_error_at(msg % e_name.id, st.st_filename, LOCATION(s))
            if s.simple:
                symtable_add_def(st, e_name.id, DEF_ANNOT | DEF_LOCAL, LOCATION(e_name))
            elif s.value is not None:
                symtable_add_def(st, e_name.id, DEF_LOCAL, LOCATION(e_name))
        else:
            symtable_visit_expr(st, s.target)
        symtable_visit_annotation(st, s.annotation, st.st_cur.ste_id + 1)
        if s.value is not None:
            symtable_visit_expr(st, s.value)
    elif isinstance(s, _ast.AugAssign):
        symtable_visit_expr(st, s.target)
        symtable_visit_expr(st, s.value)
    elif isinstance(s, (_ast.For, _ast.AsyncFor)):
        if isinstance(s, _ast.AsyncFor):
            maybe_set_ste_coroutine_for_module(st, s)
            symtable_raise_if_not_coroutine(st, ASYNC_FOR_OUTSIDE_ASYNC_FUNC, LOCATION(s))
        symtable_visit_expr(st, s.target)
        symtable_visit_expr(st, s.iter)
        with ConditionalBlock(st):
            for stmt in s.body:
                symtable_visit_stmt(st, stmt)
            for stmt in s.orelse:
                symtable_visit_stmt(st, stmt)
    elif isinstance(s, (_ast.While, _ast.If)):
        symtable_visit_expr(st, s.test)
        with ConditionalBlock(st):
            for stmt in s.body:
                symtable_visit_stmt(st, stmt)
            for stmt in s.orelse:
                symtable_visit_stmt(st, stmt)
    elif isinstance(s, _ast.Match):
        symtable_visit_expr(st, s.subject)
        with ConditionalBlock(st):
            for m in s.cases:
                symtable_visit_match_case(st, m)
    elif isinstance(s, _ast.Raise):
        if s.exc is not None:
            symtable_visit_expr(st, s.exc)
            if s.cause is not None:
                symtable_visit_expr(st, s.cause)
    elif isinstance(s, (_ast.Try, _ast.TryStar)):
        with ConditionalBlock(st):
            for stmt in s.body:
                symtable_visit_stmt(st, stmt)
            for eh in s.handlers:
                symtable_visit_excepthandler(st, eh)
            for stmt in s.orelse:
                symtable_visit_stmt(st, stmt)
            for stmt in s.finalbody:
                symtable_visit_stmt(st, stmt)
    elif isinstance(s, _ast.Assert):
        symtable_visit_expr(st, s.test)
        if s.msg is not None:
            symtable_visit_expr(st, s.msg)
    elif isinstance(s, _ast.Import):
        for a in s.names:
            symtable_visit_alias(st, a)
    elif isinstance(s, _ast.ImportFrom):
        for a in s.names:
            symtable_visit_alias(st, a)
        check_import_from(st, s)
    elif isinstance(s, (_ast.Global, _ast.Nonlocal)):
        is_global = isinstance(s, _ast.Global)
        for name in s.names:
            cur = symtable_lookup(st, name)
            if cur & (DEF_PARAM | DEF_LOCAL | USE | DEF_ANNOT):
                if cur & DEF_PARAM:
                    msg = GLOBAL_PARAM if is_global else NONLOCAL_PARAM
                elif cur & USE:
                    msg = GLOBAL_AFTER_USE if is_global else NONLOCAL_AFTER_USE
                elif cur & DEF_ANNOT:
                    msg = GLOBAL_ANNOT if is_global else NONLOCAL_ANNOT
                else:  # DEF_LOCAL
                    msg = GLOBAL_AFTER_ASSIGN if is_global else NONLOCAL_AFTER_ASSIGN
                raise syntax_error_at(msg % name, st.st_filename, LOCATION(s))
            symtable_add_def(st, name, DEF_GLOBAL if is_global else DEF_NONLOCAL, LOCATION(s))
            symtable_record_directive(st, name, LOCATION(s))
    elif isinstance(s, _ast.Expr):
        symtable_visit_expr(st, s.value)
    elif isinstance(s, (_ast.Pass, _ast.Break, _ast.Continue)):
        pass
    elif isinstance(s, (_ast.With, _ast.AsyncWith)):
        if isinstance(s, _ast.AsyncWith):
            maybe_set_ste_coroutine_for_module(st, s)
            symtable_raise_if_not_coroutine(st, ASYNC_WITH_OUTSIDE_ASYNC_FUNC, LOCATION(s))
        with ConditionalBlock(st):
            for item in s.items:
                symtable_visit_withitem(st, item)
            for stmt in s.body:
                symtable_visit_stmt(st, stmt)
    elif isinstance(s, _ast.AsyncFunctionDef):
        visit_function_def(st, s, True)


def symtable_extend_namedexpr_scope(st, e):
    assert isinstance(e, _ast.Name)
    target_name = e.id
    # Iterate over the stack in reverse and add to the nearest adequate scope
    for ste in reversed(st.st_stack):
        # If we find a comprehension scope, check for a target binding
        # conflict with iteration variables, otherwise skip it
        if ste.ste_comprehension:
            target_in_scope = symtable_lookup_entry(st, ste, target_name)
            if (target_in_scope & DEF_COMP_ITER) and (target_in_scope & DEF_LOCAL):
                raise syntax_error_at(NAMED_EXPR_COMP_CONFLICT % target_name, st.st_filename, LOCATION(e))
            continue
        # If we find a FunctionBlock entry, add as GLOBAL/LOCAL or NONLOCAL/LOCAL
        if ste.ste_type == FunctionBlock:
            target_in_scope = symtable_lookup_entry(st, ste, target_name)
            if target_in_scope & DEF_GLOBAL:
                symtable_add_def(st, target_name, DEF_GLOBAL, LOCATION(e))
            else:
                symtable_add_def(st, target_name, DEF_NONLOCAL, LOCATION(e))
            symtable_record_directive(st, target_name, LOCATION(e))
            return symtable_add_def_helper(st, target_name, DEF_LOCAL, ste, LOCATION(e))
        # If we find a ModuleBlock entry, add as GLOBAL
        if ste.ste_type == ModuleBlock:
            symtable_add_def(st, target_name, DEF_GLOBAL, LOCATION(e))
            symtable_record_directive(st, target_name, LOCATION(e))
            return symtable_add_def_helper(st, target_name, DEF_GLOBAL, ste, LOCATION(e))
        # Disallow usage in ClassBlock and type scopes
        msg = {ClassBlock: NAMED_EXPR_COMP_IN_CLASS,
               TypeParametersBlock: NAMED_EXPR_COMP_IN_TYPEPARAM,
               TypeAliasBlock: NAMED_EXPR_COMP_IN_TYPEALIAS,
               TypeVariableBlock: NAMED_EXPR_COMP_IN_TYPEVAR_BOUND}.get(ste.ste_type)
        if msg is not None:
            raise syntax_error_at(msg, st.st_filename, LOCATION(e))
    # We should always find either a function-like block, ModuleBlock or
    # ClassBlock and should never fall to this case
    raise SystemError("unreachable")


def symtable_handle_namedexpr(st, e):
    if st.st_cur.ste_comp_iter_expr > 0:
        # Assignment isn't allowed in a comprehension iterable expression
        raise syntax_error_at(NAMED_EXPR_COMP_ITER_EXPR, st.st_filename, LOCATION(e))
    if st.st_cur.ste_comprehension:
        # Inside a comprehension body, so find the right target scope
        symtable_extend_namedexpr_scope(st, e.target)
    symtable_visit_expr(st, e.value)
    symtable_visit_expr(st, e.target)


def symtable_visit_expr(st, e):
    if isinstance(e, _ast.Name):
        if not st.st_cur.ste_in_unevaluated_annotation:
            ctx = type(e.ctx)
            symtable_add_def_ctx(st, e.id, USE if ctx is _ast.Load else DEF_LOCAL, LOCATION(e), ctx)
            # Special-case super: it counts as a use of __class__
            if ctx is _ast.Load and is_function_like(st.st_cur) and e.id == "super":
                symtable_add_def(st, "__class__", USE, LOCATION(e))
    elif isinstance(e, _ast.Constant):
        pass
    elif isinstance(e, _ast.Attribute):
        check_name(st, e.attr, LOCATION(e), type(e.ctx))
        symtable_visit_expr(st, e.value)
    elif isinstance(e, _ast.Call):
        symtable_visit_expr(st, e.func)
        for a in e.args:
            symtable_visit_expr(st, a)
        check_keywords(st, e.keywords)
        for k in e.keywords:
            if k is not None:
                symtable_visit_keyword(st, k)
    elif isinstance(e, _ast.NamedExpr):
        symtable_raise_if_annotation_block(st, "named expression", e)
        symtable_handle_namedexpr(st, e)
    elif isinstance(e, _ast.BoolOp):
        for v in e.values:
            symtable_visit_expr(st, v)
    elif isinstance(e, _ast.BinOp):
        symtable_visit_expr(st, e.left)
        symtable_visit_expr(st, e.right)
    elif isinstance(e, _ast.UnaryOp):
        symtable_visit_expr(st, e.operand)
    elif isinstance(e, _ast.Lambda):
        for d in e.args.defaults:
            symtable_visit_expr(st, d)
        for d in e.args.kw_defaults:
            if d is not None:
                symtable_visit_expr(st, d)
        symtable_enter_block(st, "lambda", FunctionBlock, id(e), LOCATION(e))
        symtable_visit_arguments(st, e.args)
        symtable_visit_expr(st, e.body)
        symtable_exit_block(st)
    elif isinstance(e, _ast.IfExp):
        symtable_visit_expr(st, e.test)
        symtable_visit_expr(st, e.body)
        symtable_visit_expr(st, e.orelse)
    elif isinstance(e, _ast.Dict):
        for k in e.keys:
            if k is not None:
                symtable_visit_expr(st, k)
        for v in e.values:
            symtable_visit_expr(st, v)
    elif isinstance(e, _ast.Set):
        for v in e.elts:
            symtable_visit_expr(st, v)
    elif isinstance(e, _ast.GeneratorExp):
        symtable_handle_comprehension(st, e, "genexpr", e.generators, e.elt, None)
    elif isinstance(e, _ast.ListComp):
        symtable_handle_comprehension(st, e, "listcomp", e.generators, e.elt, None)
    elif isinstance(e, _ast.SetComp):
        symtable_handle_comprehension(st, e, "setcomp", e.generators, e.elt, None)
    elif isinstance(e, _ast.DictComp):
        symtable_handle_comprehension(st, e, "dictcomp", e.generators, e.key, e.value)
    elif isinstance(e, (_ast.Yield, _ast.YieldFrom)):
        symtable_raise_if_annotation_block(st, "yield expression", e)
        if e.value is not None:
            symtable_visit_expr(st, e.value)
        st.st_cur.ste_generator = 1
        if st.st_cur.ste_comprehension:
            symtable_raise_if_comprehension_block(st, e)
    elif isinstance(e, _ast.Await):
        symtable_raise_if_annotation_block(st, "await expression", e)
        if not allows_top_level_await(st):
            if not is_function_like(st.st_cur):
                raise syntax_error_at("'await' outside function", st.st_filename, LOCATION(e))
            if not IS_ASYNC_DEF(st) and st.st_cur.ste_comprehension == NoComprehension:
                raise syntax_error_at("'await' outside async function", st.st_filename, LOCATION(e))
        symtable_visit_expr(st, e.value)
        st.st_cur.ste_coroutine = 1
    elif isinstance(e, _ast.Compare):
        symtable_visit_expr(st, e.left)
        for c in e.comparators:
            symtable_visit_expr(st, c)
    elif isinstance(e, (_ast.FormattedValue, _ast.Interpolation)):
        symtable_visit_expr(st, e.value)
        if e.format_spec is not None:
            symtable_visit_expr(st, e.format_spec)
    elif isinstance(e, (_ast.JoinedStr, _ast.TemplateStr)):
        for v in e.values:
            symtable_visit_expr(st, v)
    elif isinstance(e, _ast.Subscript):
        symtable_visit_expr(st, e.value)
        symtable_visit_expr(st, e.slice)
    elif isinstance(e, _ast.Starred):
        symtable_visit_expr(st, e.value)
    elif isinstance(e, _ast.Slice):
        if e.lower is not None:
            symtable_visit_expr(st, e.lower)
        if e.upper is not None:
            symtable_visit_expr(st, e.upper)
        if e.step is not None:
            symtable_visit_expr(st, e.step)
    elif isinstance(e, (_ast.List, _ast.Tuple)):
        # child nodes of List and Tuple will have expr_context set
        for v in e.elts:
            symtable_visit_expr(st, v)


def symtable_visit_type_param_bound_or_default(st, e, name, key, tp, ste_scope_info):
    if name == "__classdict__":
        raise syntax_error_at(f"reserved name '{name}' cannot be used for type parameter",
                              st.st_filename, LOCATION(tp))
    if e is not None:
        is_in_class = st.st_cur.ste_can_see_class_scope
        symtable_enter_block(st, name, TypeVariableBlock, key, LOCATION(e))
        st.st_cur.ste_can_see_class_scope = is_in_class
        if is_in_class:
            symtable_add_def(st, "__classdict__", USE, LOCATION(e))
        assert ste_scope_info is not None
        st.st_cur.ste_scope_info = ste_scope_info
        symtable_visit_expr(st, e)
        symtable_exit_block(st)


def symtable_visit_type_param(st, tp):
    symtable_add_def(st, tp.name, DEF_TYPE_PARAM | DEF_LOCAL, LOCATION(tp))
    if isinstance(tp, _ast.TypeVar):
        ste_scope_info = None
        bound = tp.bound
        if bound is not None:
            ste_scope_info = "a TypeVar constraint" if isinstance(bound, _ast.Tuple) else "a TypeVar bound"
        # We must use a different key for the bound and default.  The obvious
        # choice would be the .bound and .default_value objects, but that
        # fails when the expression immediately inside the bound or default
        # is a comprehension: we would reuse the same key for the
        # comprehension scope.  Therefore, use the address + 1 as the second
        # key.  The only requirement for the key is that it is unique and it
        # matches the logic in codegen where the scope is retrieved.
        symtable_visit_type_param_bound_or_default(st, tp.bound, tp.name, id(tp), tp, ste_scope_info)
        symtable_visit_type_param_bound_or_default(st, tp.default_value, tp.name, id(tp) + 1, tp,
                                                   "a TypeVar default")
    elif isinstance(tp, _ast.TypeVarTuple):
        symtable_visit_type_param_bound_or_default(st, tp.default_value, tp.name, id(tp), tp,
                                                   "a TypeVarTuple default")
    elif isinstance(tp, _ast.ParamSpec):
        symtable_visit_type_param_bound_or_default(st, tp.default_value, tp.name, id(tp), tp,
                                                   "a ParamSpec default")


def symtable_visit_pattern(st, p):
    if isinstance(p, _ast.MatchValue):
        symtable_visit_expr(st, p.value)
    elif isinstance(p, _ast.MatchSingleton):
        pass
    elif isinstance(p, _ast.MatchSequence):
        for q in p.patterns:
            symtable_visit_pattern(st, q)
    elif isinstance(p, _ast.MatchStar):
        if p.name:
            symtable_add_def(st, p.name, DEF_LOCAL, LOCATION(p))
    elif isinstance(p, _ast.MatchMapping):
        for k in p.keys:
            symtable_visit_expr(st, k)
        for q in p.patterns:
            symtable_visit_pattern(st, q)
        if p.rest:
            symtable_add_def(st, p.rest, DEF_LOCAL, LOCATION(p))
    elif isinstance(p, _ast.MatchClass):
        symtable_visit_expr(st, p.cls)
        for q in p.patterns:
            symtable_visit_pattern(st, q)
        check_kwd_patterns(st, p)
        for q in p.kwd_patterns:
            symtable_visit_pattern(st, q)
    elif isinstance(p, _ast.MatchAs):
        if p.pattern is not None:
            symtable_visit_pattern(st, p.pattern)
        if p.name:
            symtable_add_def(st, p.name, DEF_LOCAL, LOCATION(p))
    elif isinstance(p, _ast.MatchOr):
        for q in p.patterns:
            symtable_visit_pattern(st, q)


def symtable_implicit_arg(st, pos):
    symtable_add_def(st, f".{pos}", DEF_PARAM, st.st_cur.ste_loc)


def symtable_visit_params(st, args):
    for arg in args:
        symtable_add_def(st, arg.arg, DEF_PARAM, LOCATION(arg))


def symtable_visit_annotation(st, annotation, key):
    # Annotations in local scopes are not executed and should not affect the symtable
    is_unevaluated = st.st_cur.ste_type == FunctionBlock

    # Module-level annotations are always considered conditional because the
    # module may be partially executed.
    if (((st.st_cur.ste_type == ClassBlock and st.st_cur.ste_in_conditional_block)
            or st.st_cur.ste_type == ModuleBlock)
            and not st.st_cur.ste_has_conditional_annotations):
        st.st_cur.ste_has_conditional_annotations = 1
        symtable_add_def(st, "__conditional_annotations__", USE, LOCATION(annotation))
    parent_ste = st.st_cur
    if parent_ste.ste_annotation_block is None:
        current_type = parent_ste.ste_type
        symtable_enter_block(st, "__annotate__", AnnotationBlock, key, LOCATION(annotation))
        parent_ste.ste_annotation_block = st.st_cur
        future_annotations = st.st_future.ff_features & CO_FUTURE_ANNOTATIONS
        if current_type == ClassBlock and not future_annotations:
            st.st_cur.ste_can_see_class_scope = 1
            parent_ste.ste_needs_classdict = 1
            symtable_add_def(st, "__classdict__", USE, LOCATION(annotation))
    else:
        symtable_enter_existing_block(st, parent_ste.ste_annotation_block, False)
    if is_unevaluated:
        st.st_cur.ste_in_unevaluated_annotation = 1
    symtable_visit_expr(st, annotation)
    if is_unevaluated:
        st.st_cur.ste_in_unevaluated_annotation = 0
    symtable_exit_block(st)


def symtable_visit_argannotations(st, args):
    for arg in args:
        if arg.annotation is not None:
            st.st_cur.ste_annotations_used = 1
            symtable_visit_expr(st, arg.annotation)


def symtable_visit_annotations(st, o, a, returns, function_ste):
    is_in_class = st.st_cur.ste_can_see_class_scope
    current_type = st.st_cur.ste_type
    symtable_enter_block(st, "__annotate__", AnnotationBlock, id(a), LOCATION(o))
    if is_in_class or current_type == ClassBlock:
        st.st_cur.ste_can_see_class_scope = 1
        symtable_add_def(st, "__classdict__", USE, LOCATION(o))
    symtable_visit_argannotations(st, a.posonlyargs)
    symtable_visit_argannotations(st, a.args)
    if a.vararg is not None and a.vararg.annotation is not None:
        st.st_cur.ste_annotations_used = 1
        symtable_visit_expr(st, a.vararg.annotation)
    if a.kwarg is not None and a.kwarg.annotation is not None:
        st.st_cur.ste_annotations_used = 1
        symtable_visit_expr(st, a.kwarg.annotation)
    symtable_visit_argannotations(st, a.kwonlyargs)
    if returns is not None:
        st.st_cur.ste_annotations_used = 1
        symtable_visit_expr(st, returns)
    symtable_exit_block(st)


def symtable_visit_arguments(st, a):
    # skip default arguments inside function block
    symtable_visit_params(st, a.posonlyargs)
    symtable_visit_params(st, a.args)
    symtable_visit_params(st, a.kwonlyargs)
    if a.vararg is not None:
        symtable_add_def(st, a.vararg.arg, DEF_PARAM, LOCATION(a.vararg))
        st.st_cur.ste_varargs = 1
    if a.kwarg is not None:
        symtable_add_def(st, a.kwarg.arg, DEF_PARAM, LOCATION(a.kwarg))
        st.st_cur.ste_varkeywords = 1


def symtable_visit_excepthandler(st, eh):
    if eh.type is not None:
        symtable_visit_expr(st, eh.type)
    if eh.name:
        symtable_add_def(st, eh.name, DEF_LOCAL, LOCATION(eh))
    for s in eh.body:
        symtable_visit_stmt(st, s)


def symtable_visit_withitem(st, item):
    symtable_visit_expr(st, item.context_expr)
    if item.optional_vars is not None:
        symtable_visit_expr(st, item.optional_vars)


def symtable_visit_match_case(st, m):
    symtable_visit_pattern(st, m.pattern)
    if m.guard is not None:
        symtable_visit_expr(st, m.guard)
    for s in m.body:
        symtable_visit_stmt(st, s)


def symtable_visit_alias(st, a):
    # Compute store_name, the name actually bound by the import operation.
    # It is different than a.name when a.name is a dotted package name
    # (e.g. spam.eggs)
    name = a.name if a.asname is None else a.asname
    store_name = name.partition(".")[0]
    if name != "*":
        symtable_add_def(st, store_name, DEF_IMPORT, LOCATION(a))
    elif st.st_cur.ste_type != ModuleBlock:
        raise syntax_error_at(IMPORT_STAR_WARNING, st.st_filename, LOCATION(a))


def symtable_visit_comprehension(st, lc):
    st.st_cur.ste_comp_iter_target = 1
    symtable_visit_expr(st, lc.target)
    st.st_cur.ste_comp_iter_target = 0
    st.st_cur.ste_comp_iter_expr += 1
    symtable_visit_expr(st, lc.iter)
    st.st_cur.ste_comp_iter_expr -= 1
    for e in lc.ifs:
        symtable_visit_expr(st, e)
    if lc.is_async:
        st.st_cur.ste_coroutine = 1


def symtable_visit_keyword(st, k):
    symtable_visit_expr(st, k.value)


def symtable_handle_comprehension(st, e, scope_name, generators, elt, value):
    is_generator = isinstance(e, _ast.GeneratorExp)
    outermost = generators[0]
    # Outermost iterator is evaluated in current scope
    st.st_cur.ste_comp_iter_expr += 1
    symtable_visit_expr(st, outermost.iter)
    st.st_cur.ste_comp_iter_expr -= 1
    # Create comprehension scope for the rest
    symtable_enter_block(st, scope_name, FunctionBlock, id(e), LOCATION(e))
    if isinstance(e, _ast.ListComp):
        st.st_cur.ste_comprehension = ListComprehension
    elif isinstance(e, _ast.SetComp):
        st.st_cur.ste_comprehension = SetComprehension
    elif isinstance(e, _ast.DictComp):
        st.st_cur.ste_comprehension = DictComprehension
    else:
        st.st_cur.ste_comprehension = GeneratorExpression
    if outermost.is_async:
        st.st_cur.ste_coroutine = 1
    # Outermost iter is received as an argument
    symtable_implicit_arg(st, 0)
    # Visit iteration variable target, and mark them as such
    st.st_cur.ste_comp_iter_target = 1
    symtable_visit_expr(st, outermost.target)
    st.st_cur.ste_comp_iter_target = 0
    # Visit the rest of the comprehension body
    for i in outermost.ifs:
        symtable_visit_expr(st, i)
    for g in generators[1:]:
        symtable_visit_comprehension(st, g)
    if value is not None:
        symtable_visit_expr(st, value)
    symtable_visit_expr(st, elt)
    st.st_cur.ste_generator = int(is_generator)
    is_async = st.st_cur.ste_coroutine and not is_generator
    symtable_exit_block(st)
    if (is_async and not IS_ASYNC_DEF(st) and st.st_cur.ste_comprehension == NoComprehension
            and not allows_top_level_await(st)):
        raise syntax_error_at("asynchronous comprehension outside of an asynchronous function",
                              st.st_filename, LOCATION(e))
    if is_async:
        st.st_cur.ste_coroutine = 1


def symtable_raise_if_annotation_block(st, name, e):
    type_ = st.st_cur.ste_type
    if type_ == AnnotationBlock:
        msg = ANNOTATION_NOT_ALLOWED % name
    elif type_ == TypeVariableBlock:
        msg = EXPR_NOT_ALLOWED_IN_TYPE_VARIABLE % (name, st.st_cur.ste_scope_info)
    elif type_ == TypeAliasBlock:
        msg = EXPR_NOT_ALLOWED_IN_TYPE_ALIAS % name
    elif type_ == TypeParametersBlock:
        msg = EXPR_NOT_ALLOWED_IN_TYPE_PARAMETERS % name
    else:
        return
    raise syntax_error_at(msg, st.st_filename, LOCATION(e))


def symtable_raise_if_comprehension_block(st, e):
    type_ = st.st_cur.ste_comprehension
    msg = ("'yield' inside list comprehension" if type_ == ListComprehension else
           "'yield' inside set comprehension" if type_ == SetComprehension else
           "'yield' inside dict comprehension" if type_ == DictComprehension else
           "'yield' inside generator expression")
    raise syntax_error_at(msg, st.st_filename, LOCATION(e))


def symtable_raise_if_not_coroutine(st, msg, loc):
    if not st.st_cur.ste_coroutine:
        raise syntax_error_at(msg, st.st_filename, loc)


def maybe_mangle(privateobj, ste, name):
    """_Py_MaybeMangle."""
    # Special case for type parameter blocks around generic classes: we want
    # to mangle type parameter names (so a type param with a private name can
    # be used inside the class body), but we don't want to mangle any other
    # names that appear within the type parameter scope.
    if ste.ste_mangled_names is not None and name not in ste.ste_mangled_names:
        return name
    return mangle(privateobj, name)


def mangle(privateobj, ident):
    """_Py_Mangle: name mangling, __private becomes _classname__private.
    This is independent from how the name is used."""
    if not isinstance(privateobj, str) or not ident.startswith("__"):
        return ident
    # Don't mangle __id__ or names with dots.
    #
    # The only time a name with a dot can occur is when we are compiling an
    # import statement that has a package name.
    if ident.endswith("__") or "." in ident:
        return ident        # Don't mangle __whatever__
    # Strip leading underscores from class name
    stripped = privateobj.lstrip("_")
    if not stripped:
        return ident        # Don't mangle if class is just underscores
    return "_" + stripped + ident


def symtable(source, filename, mode):
    """_symtable.symtable: the top entry of source's symbol table."""
    import compiler
    if isinstance(source, (bytes, bytearray, memoryview)):
        source = bytes(source)
    elif not isinstance(source, str):
        raise TypeError(f"symtable() arg 1 must be a string or bytes object, not {type(source).__name__}")
    if mode not in ("exec", "eval", "single"):
        raise ValueError("symtable() arg 3 must be 'exec' or 'eval' or 'single'")
    mod = compiler.parse(source, filename, mode)
    future = future_from_ast(mod, filename)
    future.ff_features |= PyCF_SOURCE_IS_UTF8
    return build(mod, filename, future).st_top
