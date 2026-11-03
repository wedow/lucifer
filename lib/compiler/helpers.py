"""What the grammar's actions call: Parser/action_helpers.c and the inline
helpers of Parser/pegen.h.

Sequences are lists.  Where the C passes a NULL sequence to an AST
constructor, the action passes [] (ast2obj turns NULL into an empty list).
"""
import sys
from _ast import (
    AST, Add, And, Attribute, BitAnd, BitOr, BitXor, Call, ClassDef, Compare, Constant, Del, Div, Eq,
    FloorDiv, FormattedValue, FunctionDef, AsyncFunctionDef, Gt, GtE, In, Interpolation, Invert, Is,
    IsNot, JoinedStr, LShift, List, Load, Lt, LtE, MatMult, Mod, Module, Mult, Name, Not, NotEq, NotIn,
    Or, Pow, RShift, Starred, Store, Sub, Subscript, TemplateStr, Tuple, TypeIgnore, UAdd, USub,
    alias, arg, arguments, keyword,
)
import _ast

from . import pegen
from .pegen import (
    RAISE_ERROR_KNOWN_LOCATION, RAISE_INDENTATION_ERROR, RAISE_SYNTAX_ERROR,
    RAISE_SYNTAX_ERROR_KNOWN_LOCATION, RAISE_SYNTAX_ERROR_KNOWN_RANGE, RAISE_SYNTAX_ERROR_ON_NEXT_TOKEN,
    RAISE_SYNTAX_ERROR_STARTING_FROM, PyPARSE_BARRY_AS_BDFL, new_identifier,
)
from .string_parser import decode_string, parse_string

# The singletons ast2obj hands out for expr_context, operators and comparisons.
Load, Store, Del = pegen.LOAD, Store(), Del()
And, Or = And(), Or()
Add, Sub, Mult, MatMult, Div, Mod, Pow = Add(), Sub(), Mult(), MatMult(), Div(), Mod(), Pow()
LShift, RShift, BitOr, BitXor, BitAnd, FloorDiv = LShift(), RShift(), BitOr(), BitXor(), BitAnd(), FloorDiv()
Invert, Not, UAdd, USub = Invert(), Not(), UAdd(), USub()
Eq, NotEq, Lt, LtE, Gt, GtE = Eq(), NotEq(), Lt(), LtE(), Gt(), GtE()
Is, IsNot, In, NotIn = Is(), IsNot(), In(), NotIn()

STAR_TARGETS, DEL_TARGETS, FOR_TARGETS = range(3)


class CmpopExprPair:
    __slots__ = ("cmpop", "expr")

    def __init__(self, cmpop, expr):
        self.cmpop = cmpop
        self.expr = expr


class KeyValuePair:
    __slots__ = ("key", "value")

    def __init__(self, key, value):
        self.key = key
        self.value = value


class KeyPatternPair:
    __slots__ = ("key", "pattern")

    def __init__(self, key, pattern):
        self.key = key
        self.pattern = pattern


class NameDefaultPair:
    __slots__ = ("arg", "value")

    def __init__(self, arg, value):
        self.arg = arg
        self.value = value


class SlashWithDefault:
    __slots__ = ("plain_names", "names_with_defaults")

    def __init__(self, plain_names, names_with_defaults):
        self.plain_names = plain_names
        self.names_with_defaults = names_with_defaults


class StarEtc:
    __slots__ = ("vararg", "kwonlyargs", "kwarg")

    def __init__(self, vararg, kwonlyargs, kwarg):
        self.vararg = vararg
        self.kwonlyargs = kwonlyargs
        self.kwarg = kwarg


class KeywordOrStarred:
    __slots__ = ("element", "is_keyword")

    def __init__(self, element, is_keyword):
        self.element = element
        self.is_keyword = is_keyword


class ResultTokenWithMetadata:
    __slots__ = ("result", "metadata")

    def __init__(self, result, metadata):
        self.result = result
        self.metadata = metadata


def EXTRA_EXPR(head, tail):
    return dict(lineno=head.lineno, col_offset=head.col_offset,
                end_lineno=tail.end_lineno, end_col_offset=tail.end_col_offset)


# pegen.h inline helpers

def tokens_are_adjacent(a, b):
    return a.end_lineno == b.lineno and a.end_col_offset == b.col_offset


def CHECK_VERSION(p, version, msg, node):
    """INVALID_VERSION_CHECK"""
    if node is None:
        return None
    if p.feature_version < version:
        RAISE_SYNTAX_ERROR(p, "%s only supported in Python 3.%i and greater", msg, version)
    return node


def NEW_TYPE_COMMENT(p, tc):
    if tc is None:
        return None
    return new_type_comment(p, tc.string)


def RAISE_SYNTAX_ERROR_INVALID_TARGET(p, type, e):
    invalid_target = get_invalid_target(e, type)
    if invalid_target is not None:
        if type == STAR_TARGETS or type == FOR_TARGETS:
            msg = "cannot assign to %s"
        else:
            msg = "cannot delete %s"
        RAISE_SYNTAX_ERROR_KNOWN_LOCATION(p, invalid_target, msg, get_expr_name(invalid_target))
    return None


# action_helpers.c

def seq_insert_in_front(p, a, seq):
    """_PyPegen_seq_insert_in_front"""
    return [a] if seq is None else [a, *seq]


def seq_append_to_end(p, seq, a):
    """_PyPegen_seq_append_to_end"""
    return [a] if seq is None else [*seq, a]


def seq_flatten(p, seqs):
    """_PyPegen_seq_flatten"""
    return [item for inner in seqs for item in inner]


def join_names_with_dot(p, first_name, second_name):
    """_PyPegen_join_names_with_dot"""
    return Name(sys.intern(f"{first_name.id}.{second_name.id}"), Load, **EXTRA_EXPR(first_name, second_name))


def seq_count_dots(seq):
    """_PyPegen_seq_count_dots"""
    return sum(3 if t.type == pegen.token.ELLIPSIS else 1 for t in seq)


def alias_for_star(p, lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_alias_for_star"""
    return alias("*", None, lineno=lineno, col_offset=col_offset, end_lineno=end_lineno,
                 end_col_offset=end_col_offset)


def map_names_to_ids(p, seq):
    """_PyPegen_map_names_to_ids"""
    return [e.id for e in seq]


def cmpop_expr_pair(p, cmpop, expr):
    return CmpopExprPair(cmpop, expr)


def get_cmpops(p, seq):
    return [pair.cmpop for pair in seq]


def get_exprs(p, seq):
    return [pair.expr for pair in seq]


def _set_seq_context(p, seq, ctx):
    return [set_expr_context(p, e, ctx) for e in seq or ()]


def set_expr_context(p, expr, ctx):
    """_PyPegen_set_expr_context: an expr equivalent to expr with ctx as context"""
    kind = type(expr)
    if kind is Name:
        return Name(expr.id, ctx, **EXTRA_EXPR(expr, expr))
    if kind is Tuple:
        return Tuple(_set_seq_context(p, expr.elts, ctx), ctx, **EXTRA_EXPR(expr, expr))
    if kind is List:
        return List(_set_seq_context(p, expr.elts, ctx), ctx, **EXTRA_EXPR(expr, expr))
    if kind is Subscript:
        return Subscript(expr.value, expr.slice, ctx, **EXTRA_EXPR(expr, expr))
    if kind is Attribute:
        return Attribute(expr.value, expr.attr, ctx, **EXTRA_EXPR(expr, expr))
    if kind is Starred:
        return Starred(set_expr_context(p, expr.value, ctx), ctx, **EXTRA_EXPR(expr, expr))
    return expr


def key_value_pair(p, key, value):
    return KeyValuePair(key, value)


def get_keys(p, seq):
    return [pair.key for pair in seq or ()]


def get_values(p, seq):
    return [pair.value for pair in seq or ()]


def key_pattern_pair(p, key, pattern):
    return KeyPatternPair(key, pattern)


def get_pattern_keys(p, seq):
    return [pair.key for pair in seq or ()]


def get_patterns(p, seq):
    return [pair.pattern for pair in seq or ()]


def name_default_pair(p, arg, value, tc):
    return NameDefaultPair(add_type_comment_to_arg(p, arg, tc), value)


def slash_with_default(p, plain_names, names_with_defaults):
    return SlashWithDefault(plain_names, names_with_defaults)


def star_etc(p, vararg, kwonlyargs, kwarg):
    return StarEtc(vararg, kwonlyargs, kwarg)


def join_sequences(p, a, b):
    return [*(a or ()), *(b or ())]


def _get_names(p, names_with_defaults):
    return [pair.arg for pair in names_with_defaults or ()]


def _get_defaults(p, names_with_defaults):
    return [pair.value for pair in names_with_defaults or ()]


def make_arguments(p, slash_without_default, slash_with_default, plain_names, names_with_default, star_etc):
    """_PyPegen_make_arguments: the arguments of the parameters rule"""
    if slash_without_default is not None:
        posonlyargs = slash_without_default
    elif slash_with_default is not None:
        posonlyargs = join_sequences(p, slash_with_default.plain_names,
                                     _get_names(p, slash_with_default.names_with_defaults))
    else:
        posonlyargs = []

    if names_with_default is not None:
        if plain_names is not None:
            posargs = join_sequences(p, plain_names, _get_names(p, names_with_default))
        else:
            posargs = _get_names(p, names_with_default)
    elif plain_names is not None:
        # With the current grammar, we never get here.
        posargs = plain_names
    else:
        posargs = []

    if slash_with_default is not None and names_with_default is not None:
        posdefaults = join_sequences(p, _get_defaults(p, slash_with_default.names_with_defaults),
                                     _get_defaults(p, names_with_default))
    elif slash_with_default is None and names_with_default is not None:
        posdefaults = _get_defaults(p, names_with_default)
    elif slash_with_default is not None and names_with_default is None:
        posdefaults = _get_defaults(p, slash_with_default.names_with_defaults)
    else:
        posdefaults = []

    vararg = None
    if star_etc is not None and star_etc.vararg is not None:
        vararg = star_etc.vararg

    if star_etc is not None and star_etc.kwonlyargs is not None:
        kwonlyargs = _get_names(p, star_etc.kwonlyargs)
        kwdefaults = _get_defaults(p, star_etc.kwonlyargs)
    else:
        kwonlyargs = []
        kwdefaults = []

    kwarg = None
    if star_etc is not None and star_etc.kwarg is not None:
        kwarg = star_etc.kwarg

    return arguments(posonlyargs, posargs, vararg, kwonlyargs, kwdefaults, kwarg, posdefaults)


def empty_arguments(p):
    """_PyPegen_empty_arguments"""
    return arguments([], [], None, [], [], None, [])


def function_def_decorators(p, decorators, function_def):
    """_PyPegen_function_def_decorators"""
    kind = AsyncFunctionDef if type(function_def) is AsyncFunctionDef else FunctionDef
    f = function_def
    return kind(f.name, f.args, f.body, decorators, f.returns, f.type_comment, f.type_params,
                lineno=f.lineno, col_offset=f.col_offset, end_lineno=f.end_lineno,
                end_col_offset=f.end_col_offset)


def class_def_decorators(p, decorators, class_def):
    """_PyPegen_class_def_decorators"""
    c = class_def
    return ClassDef(c.name, c.bases, c.keywords, c.body, decorators, c.type_params,
                    lineno=c.lineno, col_offset=c.col_offset, end_lineno=c.end_lineno,
                    end_col_offset=c.end_col_offset)


def keyword_or_starred(p, element, is_keyword):
    return KeywordOrStarred(element, is_keyword)


def seq_extract_starred_exprs(p, kwargs):
    """_PyPegen_seq_extract_starred_exprs ([] where the C returns NULL)"""
    return [k.element for k in kwargs if not k.is_keyword]


def seq_delete_starred_exprs(p, kwargs):
    """_PyPegen_seq_delete_starred_exprs ([] where the C returns NULL)"""
    return [k.element for k in kwargs if k.is_keyword]


def ensure_imaginary(p, exp):
    if type(exp) is not Constant or type(exp.value) is not complex:
        RAISE_SYNTAX_ERROR_KNOWN_LOCATION(p, exp, "imaginary number required in complex literal")
    return exp


def ensure_real(p, exp):
    if type(exp) is not Constant or type(exp.value) is complex:
        RAISE_SYNTAX_ERROR_KNOWN_LOCATION(p, exp, "real number required in complex literal")
    return exp


def make_module(p, a):
    """_PyPegen_make_module"""
    type_ignores = [TypeIgnore(lineno, new_type_comment(p, comment))
                    for lineno, comment in p.type_ignore_comments]
    return Module(a or [], type_ignores)


def new_type_comment(p, s):
    return s


def add_type_comment_to_arg(p, a, tc):
    if tc is None:
        return a
    return arg(a.arg, a.annotation, new_type_comment(p, tc.string),
               lineno=a.lineno, col_offset=a.col_offset, end_lineno=a.end_lineno,
               end_col_offset=a.end_col_offset)


def check_barry_as_flufl(p, t):
    """_PyPegen_check_barry_as_flufl: nonzero (true) on failure"""
    tok_str = t.string
    if p.flags & PyPARSE_BARRY_AS_BDFL and tok_str != "<>":
        RAISE_SYNTAX_ERROR(p, "with Barry as BDFL, use '<>' instead of '!='")
    if not (p.flags & PyPARSE_BARRY_AS_BDFL):
        return tok_str != "!="
    return False


def check_legacy_stmt(p, name):
    if type(name) is not Name:
        return False
    return name.id in ("print", "exec")


def _string_prefix(p):
    """TOK_GET_STRING_PREFIX"""
    tok = p.tok
    return "t" if tok.tok_mode_stack[tok.tok_mode_stack_index].string_kind == 1 else "f"


def check_fstring_conversion(p, conv_token, conv):
    """_PyPegen_check_fstring_conversion"""
    if conv_token.lineno != conv.lineno or conv_token.end_col_offset != conv.col_offset:
        RAISE_SYNTAX_ERROR_KNOWN_RANGE(p, conv_token, conv,
                                       "%c-string: conversion type must come right after the exclamation mark",
                                       _string_prefix(p))
    if len(conv.id) > 1 or conv.id[0] not in "sra":
        RAISE_SYNTAX_ERROR_KNOWN_LOCATION(
            p, conv, "%c-string: invalid conversion character %r: expected 's', 'r', or 'a'",
            _string_prefix(p), conv.id)
    return ResultTokenWithMetadata(conv, conv_token.metadata)


def _is_empty_str(item):
    return type(item) is Constant and type(item.value) is str and not item.value


def setup_full_format_spec(p, colon, spec, lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_setup_full_format_spec"""
    if spec is None:
        return None
    # This is needed to keep compatibility with 3.11, where an empty format
    # spec is parsed as an *empty* JoinedStr node, instead of having an empty
    # constant in it.
    spec = [item for item in spec if not _is_empty_str(item)]
    if not spec or (len(spec) == 1 and type(spec[0]) is Constant):
        res = JoinedStr(spec, lineno=lineno, col_offset=col_offset, end_lineno=end_lineno,
                        end_col_offset=end_col_offset)
    else:
        res = concatenate_strings(p, spec, lineno, col_offset, end_lineno, end_col_offset)
    return ResultTokenWithMetadata(res, colon.metadata)


_EXPR_NAMES = {
    "Attribute": "attribute", "Subscript": "subscript", "Starred": "starred", "Name": "name",
    "List": "list", "Tuple": "tuple", "Lambda": "lambda", "Call": "function call",
    "BoolOp": "expression", "BinOp": "expression", "UnaryOp": "expression",
    "GeneratorExp": "generator expression", "Yield": "yield expression",
    "YieldFrom": "yield expression", "Await": "await expression", "ListComp": "list comprehension",
    "SetComp": "set comprehension", "DictComp": "dict comprehension", "Dict": "dict literal",
    "Set": "set display", "JoinedStr": "f-string expression", "FormattedValue": "f-string expression",
    "TemplateStr": "t-string expression", "Interpolation": "t-string expression",
    "Compare": "comparison", "IfExp": "conditional expression", "NamedExpr": "named expression",
}


def get_expr_name(e):
    """_PyPegen_get_expr_name"""
    kind = type(e).__name__
    if kind == "Constant":
        value = e.value
        if value is None:
            return "None"
        if value is False:
            return "False"
        if value is True:
            return "True"
        if value is ...:
            return "ellipsis"
        return "literal"
    if kind in _EXPR_NAMES:
        return _EXPR_NAMES[kind]
    raise SystemError(f"unexpected expression in assignment {kind} (line {e.lineno})")


def get_last_comprehension_item(comprehension):
    if not comprehension.ifs:
        return comprehension.iter
    return comprehension.ifs[-1]


def collect_call_seqs(p, a, b, lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_collect_call_seqs"""
    if b is None:
        return Call(p.dummy_name(), a, [], lineno=lineno, col_offset=col_offset,
                    end_lineno=end_lineno, end_col_offset=end_col_offset)
    starreds = seq_extract_starred_exprs(p, b)
    keywords = seq_delete_starred_exprs(p, b)
    return Call(p.dummy_name(), [*a, *starreds], keywords, lineno=lineno, col_offset=col_offset,
                end_lineno=end_lineno, end_col_offset=end_col_offset)


# AST Error reporting helpers

def get_invalid_target(e, targets_type):
    """_PyPegen_get_invalid_target"""
    if e is None:
        return None
    # We only need to visit List and Tuple nodes recursively as those are the
    # only ones that can contain valid names in targets when they are parsed
    # as expressions. Any other kind of expression that is a container (like
    # Sets or Dicts) is directly invalid and we don't need to visit it
    # recursively.
    kind = type(e)
    if kind is List or kind is Tuple:
        for other in e.elts:
            child = get_invalid_target(other, targets_type)
            if child is not None:
                return child
        return None
    if kind is Starred:
        if targets_type == DEL_TARGETS:
            return e
        return get_invalid_target(e.value, targets_type)
    if kind is Compare:
        # This is needed, because the `a in b` in `for a in b` gets parsed as
        # a comparison, and so we need to search the left side of the
        # comparison for invalid targets.
        if targets_type == FOR_TARGETS:
            if type(e.ops[0]) is type(In):
                return get_invalid_target(e.left, targets_type)
            return None
        return e
    if kind is Name or kind is Subscript or kind is Attribute:
        return None
    return e


def arguments_parsing_error(p, e):
    """_PyPegen_arguments_parsing_error"""
    kwarg_unpacking = any(k.arg is None for k in e.keywords)
    if kwarg_unpacking:
        msg = "positional argument follows keyword argument unpacking"
    else:
        msg = "positional argument follows keyword argument"
    RAISE_SYNTAX_ERROR(p, msg)


def nonparen_genexp_in_call(p, args, comprehensions):
    """_PyPegen_nonparen_genexp_in_call

    The rule that calls this function is 'args for_if_clauses'.  For the
    input f(L, x for x in y), L and x are in args and the for is parsed as a
    for_if_clause. We have to check if len <= 1, so that input like
    dict((a, b) for a, b in x) gets successfully parsed and then we pass the
    last argument (x in the above example) as the location of the error"""
    if len(args.args) <= 1:
        return None
    RAISE_SYNTAX_ERROR_KNOWN_RANGE(p, args.args[-1], get_last_comprehension_item(comprehensions[-1]),
                                   "Generator expression must be parenthesized")


# Fstring stuff

def _decode_fstring_part(p, is_raw, constant, token):
    bstr = constant.value
    length = 1 if bstr in ("{{", "}}") else len(bstr)
    is_raw = is_raw or "\\" not in bstr
    try:
        s = decode_string(p, is_raw, bstr[:length], token)
    except (UnicodeError, ValueError) as exc:
        p.raise_decode_error(exc, constant)
    return Constant(s, None, lineno=constant.lineno, col_offset=constant.col_offset,
                    end_lineno=constant.end_lineno, end_col_offset=constant.end_col_offset)


def _get_resized_exprs(p, a, raw_expressions, b, tstring):
    is_raw = "r" in a.string or "R" in a.string
    seq = []
    for item in raw_expressions:
        # This should correspond to a JoinedStr node of two elements created
        # by formatted_value. This situation can only be the result of a
        # (f|t)-string debug expression where the first element is a constant
        # with the text and the second a formatted value with the expression.
        if type(item) is JoinedStr:
            values = item.values
            if len(values) != 2:
                raise SystemError(
                    ("unexpected TemplateStr node without debug data in t-string at line %d" if tstring
                     else "unexpected JoinedStr node without debug data in f-string at line %d") % item.lineno)
            seq.append(values[0])
            seq.append(values[1])
            continue
        if type(item) is Constant:
            item = _decode_fstring_part(p, is_raw, item, b)
            # Tokenizer emits string parts even when the underlying string
            # might become an empty value (e.g. FSTRING_MIDDLE with the value
            # \\n) so we need to check for them and simplify it here.
            if type(item.value) is str and not item.value:
                continue
        seq.append(item)
    return seq


def template_str(p, a, raw_expressions, b):
    """_PyPegen_template_str"""
    return TemplateStr(_get_resized_exprs(p, a, raw_expressions, b, True), lineno=a.lineno,
                       col_offset=a.col_offset, end_lineno=b.end_lineno, end_col_offset=b.end_col_offset)


def joined_str(p, a, raw_expressions, b):
    """_PyPegen_joined_str"""
    return JoinedStr(_get_resized_exprs(p, a, raw_expressions, b, False), lineno=a.lineno,
                     col_offset=a.col_offset, end_lineno=b.end_lineno, end_col_offset=b.end_col_offset)


def decoded_constant_from_token(p, tok):
    """_PyPegen_decoded_constant_from_token"""
    # Check if we're inside a raw f-string for format spec decoding
    is_raw = False
    t = p.tok
    if t.tok_mode_stack_index > 0:
        is_raw = bool(t.tok_mode_stack[t.tok_mode_stack_index].raw)
    s = decode_string(p, is_raw, tok.string, tok)
    return Constant(s, None, lineno=tok.lineno, col_offset=tok.col_offset,
                    end_lineno=tok.end_lineno, end_col_offset=tok.end_col_offset)


def constant_from_token(p, tok):
    """_PyPegen_constant_from_token"""
    return Constant(tok.string.split("\0", 1)[0], None, lineno=tok.lineno, col_offset=tok.col_offset,
                    end_lineno=tok.end_lineno, end_col_offset=tok.end_col_offset)


def constant_from_string(p, tok):
    """_PyPegen_constant_from_string"""
    try:
        s = parse_string(p, tok)
    except (UnicodeError, ValueError) as exc:
        p.raise_decode_error(exc)
    kind = new_identifier(p, "u") if tok.string[0] == "u" else None
    return Constant(s, kind, lineno=tok.lineno, col_offset=tok.col_offset,
                    end_lineno=tok.end_lineno, end_col_offset=tok.end_col_offset)


def _get_interpolation_conversion(p, debug, conversion, format):
    if conversion is not None:
        return ord(conversion.result.id[0])
    if debug is not None and format is None:
        # If no conversion is specified, use !r for debug expressions
        return ord("r")
    return -1


def _strip_interpolation_debug_expr(exprstr):
    # Discard whitespace and explicit line continuations after the debug "="
    # but preserve whitespace before it.
    length = len(exprstr)
    while length > 0:
        has_newline = False
        while length > 0:
            c = exprstr[length - 1]
            if not c.isspace():
                break
            if c in "\r\n":
                has_newline = True
            length -= 1
        if not has_newline or length == 0 or exprstr[length - 1] != "\\":
            break
        length -= 1
    # Preserve unexpected metadata instead of dropping source text.
    if length == 0 or exprstr[length - 1] != "=":
        return exprstr
    return exprstr[:length - 1]


def _debug_end(conversion, format, closing_brace, end_lineno, end_col_offset):
    """The end of the debug text and the metadata holding it."""
    if conversion is not None:
        return conversion.result.lineno, conversion.result.col_offset, conversion.metadata
    if format is not None:
        return format.result.lineno, format.result.col_offset + 1, format.metadata
    return end_lineno, end_col_offset, closing_brace.metadata


def interpolation(p, expression, debug, conversion, format, closing_brace,
                  lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_interpolation"""
    conversion_val = _get_interpolation_conversion(p, debug, conversion, format)
    debug_end_line, debug_end_offset, exprstr = _debug_end(conversion, format, closing_brace,
                                                           end_lineno, end_col_offset)
    final_exprstr = _strip_interpolation_debug_expr(exprstr) if debug is not None else exprstr
    result = Interpolation(expression, final_exprstr, conversion_val,
                           format.result if format is not None else None,
                           lineno=lineno, col_offset=col_offset, end_lineno=end_lineno,
                           end_col_offset=end_col_offset)
    if debug is None:
        return result
    debug_text = Constant(exprstr, None, lineno=lineno, col_offset=col_offset + 1,
                          end_lineno=debug_end_line, end_col_offset=debug_end_offset - 1)
    return JoinedStr([debug_text, result], lineno=lineno, col_offset=col_offset,
                     end_lineno=debug_end_line, end_col_offset=debug_end_offset)


def formatted_value(p, expression, debug, conversion, format, closing_brace,
                    lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_formatted_value"""
    conversion_val = _get_interpolation_conversion(p, debug, conversion, format)
    result = FormattedValue(expression, conversion_val, format.result if format is not None else None,
                            lineno=lineno, col_offset=col_offset, end_lineno=end_lineno,
                            end_col_offset=end_col_offset)
    if debug is None:
        return result
    debug_end_line, debug_end_offset, debug_metadata = _debug_end(conversion, format, closing_brace,
                                                                  end_lineno, end_col_offset)
    debug_text = Constant(debug_metadata, None, lineno=lineno, col_offset=col_offset + 1,
                          end_lineno=debug_end_line, end_col_offset=debug_end_offset - 1)
    return JoinedStr([debug_text, result], lineno=lineno, col_offset=col_offset,
                     end_lineno=debug_end_line, end_col_offset=debug_end_offset)


def _build_concatenated_str(p, strings):
    flattened = []
    for elem in strings:
        kind = type(elem)
        if kind is JoinedStr or kind is TemplateStr:
            flattened.extend(elem.values)
        else:
            flattened.append(elem)

    values = []
    i, n = 0, len(flattened)
    while i < n:
        elem = flattened[i]
        # if the current elem and the following are constants, fold them and
        # all consequent constants
        if type(elem) is Constant:
            if i + 1 < n and type(flattened[i + 1]) is Constant:
                first_elem = last_elem = elem
                # When a string is getting concatenated, the kind of the
                # string is determined by the first string in the
                # concatenation sequence.
                #
                # u"abc" "def" -> u"abcdef"
                # "abc" u"abc" ->  "abcabc"
                kind = elem.kind
                parts = []
                j = i
                while j < n and type(flattened[j]) is Constant:
                    parts.append(flattened[j].value)
                    last_elem = flattened[j]
                    j += 1
                i = j - 1
                elem = Constant("".join(parts), kind, lineno=first_elem.lineno,
                                col_offset=first_elem.col_offset, end_lineno=last_elem.end_lineno,
                                end_col_offset=last_elem.end_col_offset)
            # Drop all empty contanst strings
            if type(elem.value) is str and not elem.value:
                i += 1
                continue
        values.append(elem)
        i += 1
    return values


def concatenate_tstrings(p, strings, lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_concatenate_tstrings"""
    return TemplateStr(_build_concatenated_str(p, strings), lineno=lineno, col_offset=col_offset,
                       end_lineno=end_lineno, end_col_offset=end_col_offset)


def concatenate_strings(p, strings, lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_concatenate_strings"""
    f_string_found = unicode_string_found = bytes_found = False
    for elem in strings:
        if type(elem) is Constant:
            if type(elem.value) is bytes:
                bytes_found = True
            else:
                unicode_string_found = True
        else:
            f_string_found = True

    # Cannot mix unicode and bytes
    if (unicode_string_found or f_string_found) and bytes_found:
        RAISE_SYNTAX_ERROR(p, "cannot mix bytes and nonbytes literals")

    # If it's only bytes or only unicode string, do a simple concat
    location = dict(lineno=lineno, col_offset=col_offset, end_lineno=end_lineno, end_col_offset=end_col_offset)
    if not f_string_found:
        if len(strings) == 1:
            return strings[0]
        joiner = b"" if bytes_found else ""
        return Constant(joiner.join(elem.value for elem in strings), strings[0].kind, **location)
    return JoinedStr(_build_concatenated_str(p, strings), **location)


def checked_future_import(p, module, names, level, lineno, col_offset, end_lineno, end_col_offset):
    """_PyPegen_checked_future_import"""
    if level == 0 and module == "__future__":
        for a in names:
            if a.name == "barry_as_FLUFL":
                p.flags |= PyPARSE_BARRY_AS_BDFL
                p.tok.barry_as_bdfl = 1
    return _ast.ImportFrom(module, names, level, lineno=lineno, col_offset=col_offset,
                           end_lineno=end_lineno, end_col_offset=end_col_offset)


def register_stmts(p, stmts):
    """_PyPegen_register_stmts"""
    if not p.call_invalid_rules or not stmts:
        return stmts
    last_stmt = stmts[-1]
    if p.last_stmt_location[0] > last_stmt.lineno:
        return stmts
    p.last_stmt_location = (last_stmt.lineno, last_stmt.col_offset, last_stmt.end_lineno,
                            last_stmt.end_col_offset)
    return stmts
