"""Annotations back to strings, for `from __future__ import annotations`.

Port of Python/ast_unparse.c: a limited unparser used during compilation, not
a full AST unparser (see ast.unparse for that).
"""
import _ast

# Priority levels
(PR_TUPLE,
 PR_TEST,            # 'if'-'else', 'lambda'
 PR_OR,              # 'or'
 PR_AND,             # 'and'
 PR_NOT,             # 'not'
 PR_CMP,             # '<', '>', '==', '>=', '<=', '!=', 'in', 'not in', 'is', 'is not'
 PR_EXPR,
 PR_BXOR,            # '^'
 PR_BAND,            # '&'
 PR_SHIFT,           # '<<', '>>'
 PR_ARITH,           # '+', '-'
 PR_TERM,            # '*', '@', '/', '%', '//'
 PR_FACTOR,          # unary '+', '-', '~'
 PR_POWER,           # '**'
 PR_AWAIT,           # 'await'
 PR_ATOM) = range(16)
PR_BOR = PR_EXPR     # '|'


def append_repr(w, obj):
    r = repr(obj)
    if (type(obj) is float and r in ("inf", "-inf")) or type(obj) is complex:
        r = r.replace("inf", "1e309")  # evaluates to inf
    w.append(r)


def append_ast_boolop(w, e, level):
    is_and = type(e.op) is _ast.And
    op = " and " if is_and else " or "
    pr = PR_AND if is_and else PR_OR
    if level > pr:
        w.append("(")
    for i, value in enumerate(e.values):
        if i > 0:
            w.append(op)
        append_ast_expr(w, value, pr + 1)
    if level > pr:
        w.append(")")


_binops = {
    _ast.Add: (" + ", PR_ARITH), _ast.Sub: (" - ", PR_ARITH), _ast.Mult: (" * ", PR_TERM),
    _ast.MatMult: (" @ ", PR_TERM), _ast.Div: (" / ", PR_TERM), _ast.Mod: (" % ", PR_TERM),
    _ast.LShift: (" << ", PR_SHIFT), _ast.RShift: (" >> ", PR_SHIFT),
    _ast.BitOr: (" | ", PR_BOR), _ast.BitXor: (" ^ ", PR_BXOR), _ast.BitAnd: (" & ", PR_BAND),
    _ast.FloorDiv: (" // ", PR_TERM), _ast.Pow: (" ** ", PR_POWER),
}


def append_ast_binop(w, e, level):
    op, pr = _binops[type(e.op)]
    rassoc = type(e.op) is _ast.Pow  # is right-associative?
    if level > pr:
        w.append("(")
    append_ast_expr(w, e.left, pr + rassoc)
    w.append(op)
    append_ast_expr(w, e.right, pr + (not rassoc))
    if level > pr:
        w.append(")")


_unaryops = {_ast.Invert: ("~", PR_FACTOR), _ast.Not: ("not ", PR_NOT),
             _ast.UAdd: ("+", PR_FACTOR), _ast.USub: ("-", PR_FACTOR)}


def append_ast_unaryop(w, e, level):
    op, pr = _unaryops[type(e.op)]
    if level > pr:
        w.append("(")
    w.append(op)
    append_ast_expr(w, e.operand, pr)
    if level > pr:
        w.append(")")


def append_ast_arg(w, arg):
    w.append(arg.arg)
    if arg.annotation:
        w.append(": ")
        append_ast_expr(w, arg.annotation, PR_TEST)


def append_ast_args(w, args):
    first = True

    def sep():
        nonlocal first
        if not first:
            w.append(", ")
        first = False

    # positional-only and positional arguments with defaults
    posonlyarg_count = len(args.posonlyargs)
    arg_count = len(args.args)
    default_count = len(args.defaults)
    for i, arg in enumerate([*args.posonlyargs, *args.args]):
        sep()
        append_ast_arg(w, arg)
        di = i - posonlyarg_count - arg_count + default_count
        if di >= 0:
            w.append("=")
            append_ast_expr(w, args.defaults[di], PR_TEST)
        if posonlyarg_count and i + 1 == posonlyarg_count:
            w.append(", /")
    # vararg, or bare '*' if no varargs but keyword-only arguments present
    if args.vararg or args.kwonlyargs:
        sep()
        w.append("*")
        if args.vararg:
            append_ast_arg(w, args.vararg)
    # keyword-only arguments
    arg_count = len(args.kwonlyargs)
    default_count = len(args.kw_defaults)
    for i, arg in enumerate(args.kwonlyargs):
        sep()
        append_ast_arg(w, arg)
        di = i - arg_count + default_count
        if di >= 0:
            default = args.kw_defaults[di]
            if default:
                w.append("=")
                append_ast_expr(w, default, PR_TEST)
    # **kwargs
    if args.kwarg:
        sep()
        w.append("**")
        append_ast_arg(w, args.kwarg)


def append_ast_lambda(w, e, level):
    if level > PR_TEST:
        w.append("(")
    n_positional = len(e.args.args) + len(e.args.posonlyargs)
    w.append("lambda " if n_positional else "lambda")
    append_ast_args(w, e.args)
    w.append(": ")
    append_ast_expr(w, e.body, PR_TEST)
    if level > PR_TEST:
        w.append(")")


def append_ast_ifexp(w, e, level):
    if level > PR_TEST:
        w.append("(")
    append_ast_expr(w, e.body, PR_TEST + 1)
    w.append(" if ")
    append_ast_expr(w, e.test, PR_TEST + 1)
    w.append(" else ")
    append_ast_expr(w, e.orelse, PR_TEST)
    if level > PR_TEST:
        w.append(")")


def append_ast_dict(w, e):
    w.append("{")
    for i, (key, value) in enumerate(zip(e.keys, e.values)):
        if i > 0:
            w.append(", ")
        if key is not None:
            append_ast_expr(w, key, PR_TEST)
            w.append(": ")
            append_ast_expr(w, value, PR_TEST)
        else:
            w.append("**")
            append_ast_expr(w, value, PR_EXPR)
    w.append("}")


def append_elts(w, open, elts, close):
    w.append(open)
    for i, elt in enumerate(elts):
        if i > 0:
            w.append(", ")
        append_ast_expr(w, elt, PR_TEST)
    w.append(close)


def append_ast_tuple(w, e, level):
    elts = e.elts
    if not elts:
        w.append("()")
        return
    if level > PR_TUPLE:
        w.append("(")
    for i, elt in enumerate(elts):
        if i > 0:
            w.append(", ")
        append_ast_expr(w, elt, PR_TEST)
    if len(elts) == 1:
        w.append(",")
    if level > PR_TUPLE:
        w.append(")")


def append_ast_comprehensions(w, generators):
    for gen in generators:
        w.append(" async for " if gen.is_async else " for ")
        append_ast_expr(w, gen.target, PR_TUPLE)
        w.append(" in ")
        append_ast_expr(w, gen.iter, PR_TEST + 1)
        for if_ in gen.ifs:
            w.append(" if ")
            append_ast_expr(w, if_, PR_TEST + 1)


def append_comp(w, open, e, close):
    """append_ast_genexp, append_ast_listcomp and append_ast_setcomp."""
    w.append(open)
    append_ast_expr(w, e.elt, PR_TEST)
    append_ast_comprehensions(w, e.generators)
    w.append(close)


def append_ast_dictcomp(w, e):
    w.append("{")
    append_ast_expr(w, e.key, PR_TEST)
    w.append(": ")
    append_ast_expr(w, e.value, PR_TEST)
    append_ast_comprehensions(w, e.generators)
    w.append("}")


_cmpops = {_ast.Eq: " == ", _ast.NotEq: " != ", _ast.Lt: " < ", _ast.LtE: " <= ",
           _ast.Gt: " > ", _ast.GtE: " >= ", _ast.Is: " is ", _ast.IsNot: " is not ",
           _ast.In: " in ", _ast.NotIn: " not in "}


def append_ast_compare(w, e, level):
    if level > PR_CMP:
        w.append("(")
    assert len(e.comparators) > 0 and len(e.comparators) == len(e.ops)
    append_ast_expr(w, e.left, PR_CMP + 1)
    for op, comparator in zip(e.ops, e.comparators):
        w.append(_cmpops[type(op)])
        append_ast_expr(w, comparator, PR_CMP + 1)
    if level > PR_CMP:
        w.append(")")


def append_ast_keyword(w, kw):
    if kw.arg is None:
        w.append("**")
    else:
        w.append(kw.arg)
        w.append("=")
    append_ast_expr(w, kw.value, PR_TEST)


def append_ast_call(w, e):
    append_ast_expr(w, e.func, PR_ATOM)
    if len(e.args) == 1 and not e.keywords and type(e.args[0]) is _ast.GeneratorExp:
        # Special case: a single generator expression.
        append_comp(w, "(", e.args[0], ")")
        return
    w.append("(")
    first = True
    for arg in e.args:
        if not first:
            w.append(", ")
        first = False
        append_ast_expr(w, arg, PR_TEST)
    for kw in e.keywords:
        if not first:
            w.append(", ")
        first = False
        append_ast_keyword(w, kw)
    w.append(")")


def append_fstring_element(w, e, is_format_spec):
    t = type(e)
    if t is _ast.Constant:
        w.append(e.value.replace("{", "{{").replace("}", "}}"))
    elif t is _ast.JoinedStr:
        append_joinedstr(w, e, is_format_spec)
    elif t is _ast.TemplateStr:
        append_templatestr(w, e)
    elif t is _ast.FormattedValue:
        append_formattedvalue(w, e)
    elif t is _ast.Interpolation:
        append_interpolation(w, e)
    else:
        raise SystemError("unknown expression kind inside f-string or t-string")


def build_ftstring_body(values, is_format_spec):
    """Build the body separately to wrap the entire stream of Strs, Constants
    and FormattedValues in one opening and one closing quote."""
    body = []
    for value in values:
        append_fstring_element(body, value, is_format_spec)
    return "".join(body)


def append_templatestr(w, e):
    body = build_ftstring_body(e.values, False)
    w.append("t")
    append_repr(w, body)


def append_joinedstr(w, e, is_format_spec):
    body = build_ftstring_body(e.values, is_format_spec)
    if not is_format_spec:
        w.append("f")
        append_repr(w, body)
    else:
        w.append(body)


def append_interpolation_str(w, s):
    # An expression starting with a brace is split with a space from the outer one.
    w.append("{ " if s.startswith("{") else "{")
    w.append(s)


def append_interpolation_value(w, e):
    # Grammar allows PR_TUPLE, but use >PR_TEST for adding parenthesis around
    # a lambda with ':'
    append_interpolation_str(w, expr_as_unicode(e, PR_TEST + 1))


def append_interpolation_conversion(w, conversion):
    if conversion < 0:
        return
    if conversion not in (ord("a"), ord("r"), ord("s")):
        raise SystemError("unknown f-value conversion kind")
    w.append("!" + chr(conversion))


def append_interpolation_format_spec(w, e):
    if e:
        w.append(":")
        append_fstring_element(w, e, True)


def append_interpolation(w, e):
    append_interpolation_str(w, e.str)
    append_interpolation_conversion(w, e.conversion)
    append_interpolation_format_spec(w, e.format_spec)
    w.append("}")


def append_formattedvalue(w, e):
    append_interpolation_value(w, e.value)
    append_interpolation_conversion(w, e.conversion)
    append_interpolation_format_spec(w, e.format_spec)
    w.append("}")


def append_ast_constant(w, constant):
    if type(constant) is tuple:
        w.append("(")
        for i, item in enumerate(constant):
            if i > 0:
                w.append(", ")
            append_ast_constant(w, item)
        if len(constant) == 1:
            w.append(",")
        w.append(")")
        return
    append_repr(w, constant)


def append_ast_attribute(w, e):
    v = e.value
    append_ast_expr(w, v, PR_ATOM)
    # Special case: integers require a space for attribute access to be
    # unambiguous.
    w.append(" ." if type(v) is _ast.Constant and type(v.value) is int else ".")
    w.append(e.attr)


def append_ast_slice(w, e):
    if e.lower:
        append_ast_expr(w, e.lower, PR_TEST)
    w.append(":")
    if e.upper:
        append_ast_expr(w, e.upper, PR_TEST)
    if e.step:
        w.append(":")
        append_ast_expr(w, e.step, PR_TEST)


def append_ast_subscript(w, e):
    append_ast_expr(w, e.value, PR_ATOM)
    w.append("[")
    append_ast_expr(w, e.slice, PR_TUPLE)
    w.append("]")


def append_ast_yield(w, e):
    if not e.value:
        w.append("(yield)")
        return
    w.append("(yield ")
    append_ast_expr(w, e.value, PR_TEST)
    w.append(")")


def append_ast_yield_from(w, e):
    w.append("(yield from ")
    append_ast_expr(w, e.value, PR_TEST)
    w.append(")")


def append_ast_await(w, e, level):
    if level > PR_AWAIT:
        w.append("(")
    w.append("await ")
    append_ast_expr(w, e.value, PR_ATOM)
    if level > PR_AWAIT:
        w.append(")")


def append_named_expr(w, e, level):
    if level > PR_TUPLE:
        w.append("(")
    append_ast_expr(w, e.target, PR_ATOM)
    w.append(" := ")
    append_ast_expr(w, e.value, PR_ATOM)
    if level > PR_TUPLE:
        w.append(")")


def append_ast_constant_expr(w, e):
    if e.value is Ellipsis:
        w.append("...")
        return
    if e.kind is not None:
        w.append(e.kind)
    append_ast_constant(w, e.value)


def append_ast_starred(w, e):
    w.append("*")
    append_ast_expr(w, e.value, PR_EXPR)


_visitors = {
    _ast.BoolOp: append_ast_boolop,
    _ast.BinOp: append_ast_binop,
    _ast.UnaryOp: append_ast_unaryop,
    _ast.Lambda: append_ast_lambda,
    _ast.IfExp: append_ast_ifexp,
    _ast.Dict: lambda w, e, level: append_ast_dict(w, e),
    _ast.Set: lambda w, e, level: append_elts(w, "{", e.elts, "}"),
    _ast.GeneratorExp: lambda w, e, level: append_comp(w, "(", e, ")"),
    _ast.ListComp: lambda w, e, level: append_comp(w, "[", e, "]"),
    _ast.SetComp: lambda w, e, level: append_comp(w, "{", e, "}"),
    _ast.DictComp: lambda w, e, level: append_ast_dictcomp(w, e),
    _ast.Yield: lambda w, e, level: append_ast_yield(w, e),
    _ast.YieldFrom: lambda w, e, level: append_ast_yield_from(w, e),
    _ast.Await: append_ast_await,
    _ast.Compare: append_ast_compare,
    _ast.Call: lambda w, e, level: append_ast_call(w, e),
    _ast.Constant: lambda w, e, level: append_ast_constant_expr(w, e),
    _ast.JoinedStr: lambda w, e, level: append_joinedstr(w, e, False),
    _ast.TemplateStr: lambda w, e, level: append_templatestr(w, e),
    _ast.FormattedValue: lambda w, e, level: append_formattedvalue(w, e),
    _ast.Interpolation: lambda w, e, level: append_interpolation(w, e),
    # The following exprs can be assignment targets.
    _ast.Attribute: lambda w, e, level: append_ast_attribute(w, e),
    _ast.Subscript: lambda w, e, level: append_ast_subscript(w, e),
    _ast.Starred: lambda w, e, level: append_ast_starred(w, e),
    _ast.Slice: lambda w, e, level: append_ast_slice(w, e),
    _ast.Name: lambda w, e, level: w.append(e.id),
    _ast.List: lambda w, e, level: append_elts(w, "[", e.elts, "]"),
    _ast.Tuple: append_ast_tuple,
    _ast.NamedExpr: append_named_expr,
}


def append_ast_expr(w, e, level):
    visitor = _visitors.get(type(e))
    if visitor is None:
        raise SystemError("unknown expression kind")
    visitor(w, e, level)


def expr_as_unicode(e, level=PR_TEST):
    """_PyAST_ExprAsUnicode (with the default level): the source of e."""
    w = []
    append_ast_expr(w, e, level)
    return "".join(w)
