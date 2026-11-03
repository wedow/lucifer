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


def compile(source, filename, mode, flags=0, dont_inherit=False, optimize=-1, *, _feature_version=-1):
    if isinstance(source, _ast.AST):
        mod = source
    else:
        mod = parse(source, filename, mode, flags, _feature_version)
    from .preprocess import preprocess
    if flags & PyCF_ONLY_AST:
        syntax_check_only = (flags & PyCF_OPTIMIZED_AST) == PyCF_ONLY_AST
        preprocess(mod, filename, flags, optimize, syntax_check_only)
        return mod
    from .codegen import compile_ast
    return compile_ast(mod, filename, flags, optimize)
