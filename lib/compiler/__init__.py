"""CPython 3.14's bytecode compiler, in Python, producing identical output.

Stages (ported from CPython in dependency order): tokenizer, parser,
preprocess (future features and constant folding), symtable, codegen,
flowgraph, assemble.  compile() follows builtins.compile
(Python/bltinmodule.c) for the modes and flags the stages support.
"""
import _ast

PyCF_ONLY_AST = _ast.PyCF_ONLY_AST
PyCF_OPTIMIZED_AST = _ast.PyCF_OPTIMIZED_AST


def parse(source, filename, mode, flags=0, feature_version=-1):
    """The AST of source, unpreprocessed (Parser/peg_api.c)."""
    from .parser import parse
    return parse(source, filename, mode, flags, feature_version)


def _fill_end_locations(mod):
    """What PyAST_obj2ast does with a tree built by hand: lineno and
    col_offset are required; end_lineno and end_col_offset default to them.
    Returns the (node, name, old value) filled in, for _restore: compiling
    does not change the caller's tree."""
    filled = []
    stack = [mod]
    while stack:
        node = stack.pop()
        if 'lineno' in node._attributes:
            for name in ('lineno', 'col_offset'):
                if getattr(node, name, None) is None:
                    raise TypeError('required field "%s" missing from %s' % (name, type(node).__mro__[1].__name__))
            for name, default in (('end_lineno', node.lineno), ('end_col_offset', node.col_offset)):
                if getattr(node, name, None) is None:
                    filled.append((node, name, node.__dict__.get(name)))
                    setattr(node, name, default)
        for name in node._fields:
            child = getattr(node, name, None)
            if isinstance(child, _ast.AST):
                stack.append(child)
            elif isinstance(child, list):
                stack.extend(c for c in child if isinstance(c, _ast.AST))
    return filled


def _restore(filled):
    for node, name, old in filled:
        setattr(node, name, old)


def compile(source, filename, mode, flags=0, dont_inherit=False, optimize=-1, *, _feature_version=-1):
    filled = ()
    if isinstance(source, _ast.AST):
        mod = source
        filled = _fill_end_locations(mod)
    else:
        if (b"\0" if isinstance(source, bytes) else "\0") in source:  # _Py_SourceAsString
            raise SyntaxError("source code string cannot contain null bytes")
        mod = parse(source, filename, mode, flags, _feature_version)
    try:
        from .preprocess import preprocess
        if flags & PyCF_ONLY_AST:
            syntax_check_only = (flags & PyCF_OPTIMIZED_AST) == PyCF_ONLY_AST
            preprocess(mod, filename, flags, optimize, syntax_check_only)
            return mod
        from .codegen import compile_ast
        return compile_ast(mod, filename, flags, optimize)
    finally:
        _restore(filled)
