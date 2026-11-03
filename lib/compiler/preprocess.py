"""AST pre-processing (Python/ast_preprocess.c and _PyCompile_AstPreprocess in
Python/compile.c): docstring removal, __debug__, %-format folding, match
pattern constants and the PEP 765 warnings.  The tree is changed in place;
where the C overwrites a node's struct, the node object here takes the new
node's class and fields, so references to it see the change."""
import _ast
import sys

from .future import emit_syntax_warning, from_ast as future_from_ast, get_docstring, CO_FUTURE_ANNOTATIONS

F_LJUST = 1 << 0
F_SIGN = 1 << 1
F_BLANK = 1 << 2
F_ALT = 1 << 3
F_ZERO = 1 << 4

MAXDIGITS = 3

LOCATION_ATTRIBUTES = ("lineno", "col_offset", "end_lineno", "end_col_offset")


class ControlFlowInFinallyContext:
    """See PEP 765."""

    def __init__(self, in_finally, in_funcdef, in_loop):
        self.in_finally = in_finally
        self.in_funcdef = in_funcdef
        self.in_loop = in_loop


class State:
    """_PyASTPreprocessState."""

    def __init__(self, filename, optimize, ff_features, syntax_check_only, enable_warnings):
        self.filename = filename
        self.optimize = optimize
        self.ff_features = ff_features
        self.syntax_check_only = syntax_check_only
        self.enable_warnings = enable_warnings
        self.cf_finally = []        # context for PEP 765 check


def control_flow_in_finally_warning(kw, n, state):
    emit_syntax_warning(f"'{kw}' in a 'finally' block", state.filename, n.lineno, n.col_offset + 1,
                        n.end_lineno, n.end_col_offset + 1)


def before_return(state, node):
    if state.enable_warnings and state.cf_finally:
        ctx = state.cf_finally[-1]
        if ctx.in_finally and not ctx.in_funcdef:
            control_flow_in_finally_warning("return", node, state)


def before_loop_exit(state, node, kw):
    if state.enable_warnings and state.cf_finally:
        ctx = state.cf_finally[-1]
        if ctx.in_finally and not ctx.in_loop:
            control_flow_in_finally_warning(kw, node, state)


def make_const(node, compute):
    """Overwrite node with Constant(compute()), unless compute raises (e.g.
    division by zero), when the node stays as it is."""
    try:
        val = compute()
    except Exception:
        return
    fields = node.__dict__
    for name in [name for name in fields if name not in LOCATION_ATTRIBUTES]:
        del fields[name]
    node.__class__ = _ast.Constant
    node.kind = None
    node.value = val


def copy_node(to, frm):
    """COPY_NODE: overwrite the node to with frm."""
    to.__class__ = frm.__class__
    to.__dict__.clear()
    to.__dict__.update(frm.__dict__)


def constant(value):
    """_PyAST_Constant(value, NULL, -1, -1, -1, -1)."""
    return _ast.Constant(value=value, kind=None, lineno=-1, col_offset=-1, end_lineno=-1, end_col_offset=-1)


def has_starred(elts):
    return any(isinstance(e, _ast.Starred) for e in elts)


def parse_literal(fmt, pos):
    """The Constant for the literal text at fmt[pos:] (or None), and the new pos."""
    start = pos
    size = len(fmt)
    has_percents = False
    while pos < size:
        if fmt[pos] != "%":
            pos += 1
        elif pos + 1 < size and fmt[pos + 1] == "%":
            has_percents = True
            pos += 2
        else:
            break
    if pos == start:
        return None, pos
    s = fmt[start:pos]
    if has_percents:
        s = s.replace("%%", "%")
    return constant(s), pos


def simple_format_arg_parse(fmt, pos):
    """(spec, flags, width, prec, pos) for the conversion at fmt[pos:], or None if unsupported."""
    length = len(fmt)
    width = prec = -1

    def nextc():
        nonlocal pos
        if pos >= length:
            raise IndexError
        ch = fmt[pos]
        pos += 1
        return ch

    try:
        flags = 0
        while True:
            ch = nextc()
            if ch == "-":
                flags |= F_LJUST
            elif ch == "+":
                flags |= F_SIGN
            elif ch == " ":
                flags |= F_BLANK
            elif ch == "#":
                flags |= F_ALT
            elif ch == "0":
                flags |= F_ZERO
            else:
                break
        if "0" <= ch <= "9":
            width = 0
            digits = 0
            while "0" <= ch <= "9":
                width = width * 10 + ord(ch) - ord("0")
                ch = nextc()
                digits += 1
                if digits >= MAXDIGITS:
                    return None
        if ch == ".":
            ch = nextc()
            prec = 0
            if "0" <= ch <= "9":
                digits = 0
                while "0" <= ch <= "9":
                    prec = prec * 10 + ord(ch) - ord("0")
                    ch = nextc()
                    digits += 1
                    if digits >= MAXDIGITS:
                        return None
    except IndexError:
        return None
    return ch, flags, width, prec, pos


def parse_format(fmt, pos, arg):
    """The FormattedValue for the conversion at fmt[pos:] (or None if unsupported), and the new pos."""
    parsed = simple_format_arg_parse(fmt, pos)
    if parsed is None:
        # Unsupported format.
        return None, pos
    spec, flags, width, prec, pos = parsed
    if spec in "sra":
        buf = ""
        if not (flags & F_LJUST) and width > 0:
            buf += ">"
        if width >= 0:
            buf += str(width)
        if prec >= 0:
            buf += f".{prec}"
        format_spec = constant(buf) if buf else None
        return _ast.FormattedValue(value=arg, conversion=ord(spec), format_spec=format_spec,
                                   lineno=arg.lineno, col_offset=arg.col_offset,
                                   end_lineno=arg.end_lineno, end_col_offset=arg.end_col_offset), pos
    # Unsupported format.
    return None, pos


def optimize_format(node, fmt, elts):
    pos = 0
    cnt = 0
    seq = []
    while True:
        lit, pos = parse_literal(fmt, pos)
        if lit is not None:
            seq.append(lit)
        if pos >= len(fmt):
            break
        if cnt >= len(elts):
            # More format units than items.
            return
        assert fmt[pos] == "%"
        pos += 1
        expr, pos = parse_format(fmt, pos, elts[cnt])
        cnt += 1
        if expr is None:
            return
        seq.append(expr)
    if cnt < len(elts):
        # More items than format units.
        return
    copy_node(node, _ast.JoinedStr(values=seq, lineno=node.lineno, col_offset=node.col_offset,
                                   end_lineno=node.end_lineno, end_col_offset=node.end_col_offset))


def fold_binop(node, state):
    if state.syntax_check_only:
        return
    lhs = node.left
    rhs = node.right
    if not isinstance(lhs, _ast.Constant):
        return
    lv = lhs.value
    if isinstance(node.op, _ast.Mod) and isinstance(rhs, _ast.Tuple) and isinstance(lv, str) \
            and not has_starred(rhs.elts):
        optimize_format(node, lv, rhs.elts)


def fold_seq(fold, seq, state):
    """CALL_SEQ: fold each element that is not None."""
    for elt in seq:
        if elt is not None:
            fold(elt, state)


def fold_opt(fold, node, state):
    """CALL_OPT."""
    if node is not None:
        fold(node, state)


def remove_docstring(stmts):
    # In case there's just the docstring in the body, replace it with `pass`
    # keyword, so body won't be empty.
    if len(stmts) == 1:
        docstring = stmts[0]
        # we know that `pass` always takes 4 chars and a single line, while
        # docstring can span on multiple lines
        stmts[0] = _ast.Pass(lineno=docstring.lineno, col_offset=docstring.col_offset,
                             end_lineno=docstring.lineno, end_col_offset=docstring.col_offset + 4)
        return
    # In case there are more than 1 body items, just remove the docstring.
    del stmts[0]


def astfold_body(stmts, state):
    docstring = get_docstring(stmts) is not None
    if docstring and state.optimize >= 2:
        # remove the docstring
        remove_docstring(stmts)
        docstring = False
    fold_seq(astfold_stmt, stmts, state)
    if not docstring and get_docstring(stmts) is not None:
        st = stmts[0]
        st.value = _ast.JoinedStr(values=[st.value], lineno=st.lineno, col_offset=st.col_offset,
                                  end_lineno=st.end_lineno, end_col_offset=st.end_col_offset)


def astfold_mod(node, state):
    if isinstance(node, _ast.Module):
        astfold_body(node.body, state)
    elif isinstance(node, _ast.Interactive):
        fold_seq(astfold_stmt, node.body, state)
    elif isinstance(node, _ast.Expression):
        astfold_expr(node.body, state)
    # FunctionType doesn't participate in constant folding


def astfold_expr(node, state):
    if isinstance(node, _ast.Name):
        if state.syntax_check_only:
            return
        if isinstance(node.ctx, _ast.Load) and node.id == "__debug__":
            make_const(node, lambda: not state.optimize)
    elif isinstance(node, _ast.Constant):
        # Already a constant, nothing further to do
        pass
    elif isinstance(node, _ast.BoolOp):
        fold_seq(astfold_expr, node.values, state)
    elif isinstance(node, _ast.BinOp):
        astfold_expr(node.left, state)
        astfold_expr(node.right, state)
        fold_binop(node, state)
    elif isinstance(node, _ast.UnaryOp):
        astfold_expr(node.operand, state)
    elif isinstance(node, _ast.Lambda):
        astfold_arguments(node.args, state)
        astfold_expr(node.body, state)
    elif isinstance(node, _ast.IfExp):
        astfold_expr(node.test, state)
        astfold_expr(node.body, state)
        astfold_expr(node.orelse, state)
    elif isinstance(node, _ast.Dict):
        fold_seq(astfold_expr, node.keys, state)
        fold_seq(astfold_expr, node.values, state)
    elif isinstance(node, _ast.Set):
        fold_seq(astfold_expr, node.elts, state)
    elif isinstance(node, (_ast.ListComp, _ast.SetComp, _ast.GeneratorExp)):
        astfold_expr(node.elt, state)
        fold_seq(astfold_comprehension, node.generators, state)
    elif isinstance(node, _ast.DictComp):
        astfold_expr(node.key, state)
        astfold_expr(node.value, state)
        fold_seq(astfold_comprehension, node.generators, state)
    elif isinstance(node, (_ast.Await, _ast.YieldFrom)):
        astfold_expr(node.value, state)
    elif isinstance(node, _ast.Yield):
        fold_opt(astfold_expr, node.value, state)
    elif isinstance(node, _ast.Compare):
        astfold_expr(node.left, state)
        fold_seq(astfold_expr, node.comparators, state)
    elif isinstance(node, _ast.Call):
        astfold_expr(node.func, state)
        fold_seq(astfold_expr, node.args, state)
        fold_seq(astfold_keyword, node.keywords, state)
    elif isinstance(node, (_ast.FormattedValue, _ast.Interpolation)):
        astfold_expr(node.value, state)
        fold_opt(astfold_expr, node.format_spec, state)
    elif isinstance(node, (_ast.JoinedStr, _ast.TemplateStr)):
        fold_seq(astfold_expr, node.values, state)
    elif isinstance(node, (_ast.Attribute, _ast.Starred)):
        astfold_expr(node.value, state)
    elif isinstance(node, _ast.Subscript):
        astfold_expr(node.value, state)
        astfold_expr(node.slice, state)
    elif isinstance(node, _ast.Slice):
        fold_opt(astfold_expr, node.lower, state)
        fold_opt(astfold_expr, node.upper, state)
        fold_opt(astfold_expr, node.step, state)
    elif isinstance(node, (_ast.List, _ast.Tuple)):
        fold_seq(astfold_expr, node.elts, state)
    elif isinstance(node, _ast.NamedExpr):
        astfold_expr(node.value, state)


def astfold_keyword(node, state):
    astfold_expr(node.value, state)


def astfold_comprehension(node, state):
    astfold_expr(node.target, state)
    astfold_expr(node.iter, state)
    fold_seq(astfold_expr, node.ifs, state)


def astfold_arguments(node, state):
    fold_seq(astfold_arg, node.posonlyargs, state)
    fold_seq(astfold_arg, node.args, state)
    fold_opt(astfold_arg, node.vararg, state)
    fold_seq(astfold_arg, node.kwonlyargs, state)
    fold_seq(astfold_expr, node.kw_defaults, state)
    fold_opt(astfold_arg, node.kwarg, state)
    fold_seq(astfold_expr, node.defaults, state)


def astfold_arg(node, state):
    if not (state.ff_features & CO_FUTURE_ANNOTATIONS):
        fold_opt(astfold_expr, node.annotation, state)


def astfold_stmt(node, state):
    if isinstance(node, (_ast.FunctionDef, _ast.AsyncFunctionDef)):
        fold_seq(astfold_type_param, node.type_params, state)
        astfold_arguments(node.args, state)
        state.cf_finally.append(ControlFlowInFinallyContext(False, True, False))
        astfold_body(node.body, state)
        state.cf_finally.pop()
        fold_seq(astfold_expr, node.decorator_list, state)
        if not (state.ff_features & CO_FUTURE_ANNOTATIONS):
            fold_opt(astfold_expr, node.returns, state)
    elif isinstance(node, _ast.ClassDef):
        fold_seq(astfold_type_param, node.type_params, state)
        fold_seq(astfold_expr, node.bases, state)
        fold_seq(astfold_keyword, node.keywords, state)
        astfold_body(node.body, state)
        fold_seq(astfold_expr, node.decorator_list, state)
    elif isinstance(node, _ast.Return):
        before_return(state, node)
        fold_opt(astfold_expr, node.value, state)
    elif isinstance(node, _ast.Delete):
        fold_seq(astfold_expr, node.targets, state)
    elif isinstance(node, _ast.Assign):
        fold_seq(astfold_expr, node.targets, state)
        astfold_expr(node.value, state)
    elif isinstance(node, _ast.AugAssign):
        astfold_expr(node.target, state)
        astfold_expr(node.value, state)
    elif isinstance(node, _ast.AnnAssign):
        astfold_expr(node.target, state)
        if not (state.ff_features & CO_FUTURE_ANNOTATIONS):
            astfold_expr(node.annotation, state)
        fold_opt(astfold_expr, node.value, state)
    elif isinstance(node, _ast.TypeAlias):
        astfold_expr(node.name, state)
        fold_seq(astfold_type_param, node.type_params, state)
        astfold_expr(node.value, state)
    elif isinstance(node, (_ast.For, _ast.AsyncFor)):
        astfold_expr(node.target, state)
        astfold_expr(node.iter, state)
        state.cf_finally.append(ControlFlowInFinallyContext(False, False, True))
        fold_seq(astfold_stmt, node.body, state)
        state.cf_finally.pop()
        fold_seq(astfold_stmt, node.orelse, state)
    elif isinstance(node, _ast.While):
        astfold_expr(node.test, state)
        state.cf_finally.append(ControlFlowInFinallyContext(False, False, True))
        fold_seq(astfold_stmt, node.body, state)
        state.cf_finally.pop()
        fold_seq(astfold_stmt, node.orelse, state)
    elif isinstance(node, _ast.If):
        astfold_expr(node.test, state)
        fold_seq(astfold_stmt, node.body, state)
        fold_seq(astfold_stmt, node.orelse, state)
    elif isinstance(node, (_ast.With, _ast.AsyncWith)):
        fold_seq(astfold_withitem, node.items, state)
        fold_seq(astfold_stmt, node.body, state)
    elif isinstance(node, _ast.Raise):
        fold_opt(astfold_expr, node.exc, state)
        fold_opt(astfold_expr, node.cause, state)
    elif isinstance(node, (_ast.Try, _ast.TryStar)):
        fold_seq(astfold_stmt, node.body, state)
        fold_seq(astfold_excepthandler, node.handlers, state)
        fold_seq(astfold_stmt, node.orelse, state)
        state.cf_finally.append(ControlFlowInFinallyContext(True, False, False))
        fold_seq(astfold_stmt, node.finalbody, state)
        state.cf_finally.pop()
    elif isinstance(node, _ast.Assert):
        astfold_expr(node.test, state)
        fold_opt(astfold_expr, node.msg, state)
    elif isinstance(node, _ast.Expr):
        astfold_expr(node.value, state)
    elif isinstance(node, _ast.Match):
        astfold_expr(node.subject, state)
        fold_seq(astfold_match_case, node.cases, state)
    elif isinstance(node, _ast.Break):
        before_loop_exit(state, node, "break")
    elif isinstance(node, _ast.Continue):
        before_loop_exit(state, node, "continue")
    # Import, ImportFrom, Global, Nonlocal and Pass contain nothing to fold


def astfold_excepthandler(node, state):
    fold_opt(astfold_expr, node.type, state)
    fold_seq(astfold_stmt, node.body, state)


def astfold_withitem(node, state):
    astfold_expr(node.context_expr, state)
    fold_opt(astfold_expr, node.optional_vars, state)


def fold_const_match_patterns(node, state):
    if state.syntax_check_only:
        return
    if isinstance(node, _ast.UnaryOp):
        if isinstance(node.op, _ast.USub) and isinstance(node.operand, _ast.Constant):
            operand = node.operand.value
            make_const(node, lambda: -operand)
    elif isinstance(node, _ast.BinOp):
        op = node.op
        if isinstance(op, (_ast.Add, _ast.Sub)) and isinstance(node.right, _ast.Constant):
            fold_const_match_patterns(node.left, state)
            if isinstance(node.left, _ast.Constant):
                left = node.left.value
                right = node.right.value
                make_const(node, (lambda: left + right) if isinstance(op, _ast.Add) else (lambda: left - right))


def astfold_pattern(node, state):
    # Currently, this is really only used to form complex/negative numeric
    # constants in MatchValue and MatchMapping nodes.  We still recurse into
    # all subexpressions and subpatterns anyway
    if isinstance(node, _ast.MatchValue):
        fold_const_match_patterns(node.value, state)
    elif isinstance(node, (_ast.MatchSequence, _ast.MatchOr)):
        fold_seq(astfold_pattern, node.patterns, state)
    elif isinstance(node, _ast.MatchMapping):
        fold_seq(fold_const_match_patterns, node.keys, state)
        fold_seq(astfold_pattern, node.patterns, state)
    elif isinstance(node, _ast.MatchClass):
        astfold_expr(node.cls, state)
        fold_seq(astfold_pattern, node.patterns, state)
        fold_seq(astfold_pattern, node.kwd_patterns, state)
    elif isinstance(node, _ast.MatchAs):
        fold_opt(astfold_pattern, node.pattern, state)
    # MatchSingleton and MatchStar contain nothing to fold


def astfold_match_case(node, state):
    astfold_pattern(node.pattern, state)
    fold_opt(astfold_expr, node.guard, state)
    fold_seq(astfold_stmt, node.body, state)


def astfold_type_param(node, state):
    if isinstance(node, _ast.TypeVar):
        fold_opt(astfold_expr, node.bound, state)
    fold_opt(astfold_expr, node.default_value, state)


def ast_preprocess(mod, filename, optimize, ff_features, syntax_check_only, enable_warnings):
    """_PyAST_Preprocess."""
    astfold_mod(mod, State(filename, optimize, ff_features, syntax_check_only, enable_warnings))


def preprocess(mod, filename, flags, optimize, syntax_check_only):
    """_PyCompile_AstPreprocess: preprocess mod in place for compile(...,
    PyCF_ONLY_AST), and return it."""
    future = future_from_ast(mod, filename)
    flags = future.ff_features | flags
    if optimize == -1:
        optimize = sys.flags.optimize
    ast_preprocess(mod, filename, optimize, flags, syntax_check_only, False)
    return mod
