"""__future__ statements (Python/future.c), and the SyntaxError helpers of
Python/errors.c that the later stages share."""
import _ast

CO_FUTURE_DIVISION = 0x20000
CO_FUTURE_ABSOLUTE_IMPORT = 0x40000
CO_FUTURE_WITH_STATEMENT = 0x80000
CO_FUTURE_PRINT_FUNCTION = 0x100000
CO_FUTURE_UNICODE_LITERALS = 0x200000
CO_FUTURE_BARRY_AS_BDFL = 0x400000
CO_FUTURE_GENERATOR_STOP = 0x800000
CO_FUTURE_ANNOTATIONS = 0x1000000

PyCF_SOURCE_IS_UTF8 = 0x0100
PyCF_ALLOW_TOP_LEVEL_AWAIT = 0x2000

# Features that need no flag: they are always on.
ACCEPTED_FEATURES = ("nested_scopes", "generators", "division", "absolute_import",
                     "with_statement", "print_function", "unicode_literals", "generator_stop")


class SourceLocation:
    """_Py_SourceLocation."""
    __slots__ = ("lineno", "end_lineno", "col_offset", "end_col_offset")

    def __init__(self, lineno, end_lineno, col_offset, end_col_offset):
        self.lineno = lineno
        self.end_lineno = end_lineno
        self.col_offset = col_offset
        self.end_col_offset = end_col_offset

    def __repr__(self):
        return f"SourceLocation({self.lineno}, {self.end_lineno}, {self.col_offset}, {self.end_col_offset})"


def LOCATION(node):
    """SRC_LOCATION_FROM_AST."""
    return SourceLocation(node.lineno, node.end_lineno, node.col_offset, node.end_col_offset)


NO_LOCATION = SourceLocation(-1, -1, -1, -1)


class FutureFeatures:
    """_PyFutureFeatures."""

    def __init__(self):
        self.ff_features = 0                # flags set by future statements
        self.ff_location = NO_LOCATION      # location of last future statement


def program_text(filename, lineno):
    """PyErr_ProgramTextObject: line lineno of the file, or None."""
    if not filename or lineno <= 0:
        return None
    try:
        with open(filename, "rb") as f:
            for i, line in enumerate(f, 1):
                if i == lineno:
                    return line.decode("utf-8", "replace")
    except (OSError, ValueError):
        pass
    return None


def ranged_syntax_error(msg, filename, lineno, col_offset, end_lineno, end_col_offset):
    """A SyntaxError located as PyErr_RangedSyntaxLocationObject does
    (col_offset and end_col_offset are already 1-based)."""
    error = SyntaxError(msg)
    error.lineno = lineno
    error.offset = col_offset if col_offset >= 0 else None
    error.end_lineno = end_lineno if end_lineno >= 0 else None
    error.end_offset = end_col_offset if end_col_offset >= 0 else None
    if filename is not None:
        error.filename = filename
        error.text = program_text(filename, lineno)
    return error


def syntax_error_at(msg, filename, loc):
    """SET_ERROR_LOCATION(filename, loc) after PyErr_SetString(SyntaxError, msg)."""
    return ranged_syntax_error(msg, filename, loc.lineno, loc.col_offset + 1,
                               loc.end_lineno, loc.end_col_offset + 1)


def raise_syntax_error(msg, filename, lineno, col_offset, end_lineno, end_col_offset):
    """_PyErr_RaiseSyntaxError."""
    raise SyntaxError(msg, (filename, lineno, col_offset, program_text(filename, lineno),
                            end_lineno, end_col_offset))


def emit_syntax_warning(msg, filename, lineno, col_offset, end_lineno, end_col_offset):
    """_PyErr_EmitSyntaxWarning: warn, or raise a SyntaxError if the warning is an error."""
    import warnings
    try:
        warnings.warn_explicit(msg, SyntaxWarning, filename, lineno)
    except SyntaxWarning:
        # Replace the SyntaxWarning exception with a SyntaxError
        # to get a more accurate error report
        raise_syntax_error(msg, filename, lineno, col_offset, end_lineno, end_col_offset)


def get_docstring(body):
    """_PyAST_GetDocString (Python/ast.c)."""
    if not body:
        return None
    st = body[0]
    if not isinstance(st, _ast.Expr):
        return None
    e = st.value
    if isinstance(e, _ast.Constant) and type(e.value) is str:
        return e.value
    return None


def future_check_features(ff, s, filename):
    for name in s.names:
        feature = name.name
        if feature in ACCEPTED_FEATURES:
            continue
        elif feature == "barry_as_FLUFL":
            ff.ff_features |= CO_FUTURE_BARRY_AS_BDFL
        elif feature == "annotations":
            ff.ff_features |= CO_FUTURE_ANNOTATIONS
        elif feature == "braces":
            raise ranged_syntax_error("not a chance", filename, name.lineno, name.col_offset + 1,
                                      name.end_lineno, name.end_col_offset + 1)
        else:
            raise ranged_syntax_error(f"future feature {feature:.100} is not defined", filename,
                                      name.lineno, name.col_offset + 1,
                                      name.end_lineno, name.end_col_offset + 1)


def future_parse(ff, mod, filename):
    if not isinstance(mod, (_ast.Module, _ast.Interactive)):
        return
    body = mod.body
    i = 1 if get_docstring(body) is not None else 0
    for s in body[i:]:
        # The only things that can precede a future statement
        # are another future statement and a doc string.
        if isinstance(s, _ast.ImportFrom) and s.level == 0 and s.module == "__future__":
            future_check_features(ff, s, filename)
            ff.ff_location = LOCATION(s)
        else:
            return


def from_ast(mod, filename):
    """_PyFuture_FromAST: the FutureFeatures of mod, or SyntaxError."""
    ff = FutureFeatures()
    future_parse(ff, mod, filename)
    return ff
