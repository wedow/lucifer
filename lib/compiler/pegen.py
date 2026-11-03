"""The PEG parser runtime: Parser/pegen.c, Parser/pegen_errors.c and pegen.h.

The generated parser (parser.py) subclasses Parser.  Errors are exceptions:
where the C code sets p->error_indicator and returns NULL, this raises, which
unwinds the parse the same way.
"""
import _ast
import builtins
import sys
import token

from . import tokenizer

ENDMARKER = token.ENDMARKER
NAME = token.NAME
NUMBER = token.NUMBER
STRING = token.STRING
NEWLINE = token.NEWLINE
INDENT = token.INDENT
DEDENT = token.DEDENT
ERRORTOKEN = token.ERRORTOKEN
TYPE_IGNORE = token.TYPE_IGNORE

SINGLE_INPUT, FILE_INPUT, EVAL_INPUT, FUNC_TYPE_INPUT = 256, 257, 258, 345
MODES = {"single": SINGLE_INPUT, "exec": FILE_INPUT, "eval": EVAL_INPUT, "func_type": FUNC_TYPE_INPUT}
START_RULES = {SINGLE_INPUT: "interactive", FILE_INPUT: "file", EVAL_INPUT: "eval",
               FUNC_TYPE_INPUT: "func_type"}

PY_MINOR_VERSION = 14

PyCF_DONT_IMPLY_DEDENT = 0x0200
PyCF_ONLY_AST = 0x0400
PyCF_IGNORE_COOKIE = 0x0800
PyCF_TYPE_COMMENTS = 0x1000
PyCF_ALLOW_INCOMPLETE_INPUT = 0x4000
CO_FUTURE_BARRY_AS_BDFL = 0x400000

PyPARSE_DONT_IMPLY_DEDENT = 0x0002
PyPARSE_IGNORE_COOKIE = 0x0010
PyPARSE_BARRY_AS_BDFL = 0x0020
PyPARSE_TYPE_COMMENTS = 0x0040
PyPARSE_ALLOW_INCOMPLETE_INPUT = 0x0100

CURRENT_POS = -5

# Parser/errcode.h
E_OK, E_EOF, E_INTR, E_TOKEN, E_SYNTAX, E_NOMEM, E_DONE, E_ERROR = range(10, 18)
E_TABSPACE, E_OVERFLOW, E_TOODEEP, E_DEDENT, E_DECODE, E_EOFS, E_EOLS, E_LINECONT = range(18, 26)
E_BADSINGLE, E_INTERACT_STOP, E_COLUMNOVERFLOW = 27, 28, 29

IncompleteInputError = getattr(builtins, "_IncompleteInputError", SyntaxError)

LOAD = _ast.Load()
# _PyRuntime.parser.dummy_name
DUMMY_NAME = _ast.Name("", LOAD, lineno=1, col_offset=0, end_lineno=1, end_col_offset=0)

STACK_OVERFLOW = "Parser stack overflowed - Python source too complex to parse"


class Token:
    """A parser token: the C struct Token, with the text as str."""
    __slots__ = ("type", "string", "level", "lineno", "col_offset", "end_lineno",
                 "end_col_offset", "memo", "metadata")

    def __init__(self, type, string, level, lineno, col_offset, end_lineno, end_col_offset, metadata):
        self.type = type
        self.string = string
        self.level = level
        self.lineno = lineno
        self.col_offset = col_offset
        self.end_lineno = end_lineno
        self.end_col_offset = end_col_offset
        self.metadata = metadata
        self.memo = {}

    def __repr__(self):
        return f"Token({self.type}, {self.string!r}, {self.lineno}:{self.col_offset})"


def memoize(method):
    """A (memo) rule: _PyPegen_is_memoized and _PyPegen_insert_memo around it."""
    name = method.__name__

    def memoize_wrapper(p):
        mark = p.mark
        if mark == p.fill:
            p.fill_token()
        memo = p.tokens[mark].memo
        if name in memo:
            res, p.mark = memo[name]
            return res
        res = method(p)
        memo[name] = res, p.mark
        return res

    memoize_wrapper.__name__ = name
    return memoize_wrapper


def memoize_left_rec(method):
    """The leader of left-recursive rules: grow the seed until it stops advancing."""
    name = method.__name__

    def memoize_left_rec_wrapper(p):
        mark = p.mark
        if mark == p.fill:
            p.fill_token()
        memo = p.tokens[mark].memo
        if name in memo:
            res, p.mark = memo[name]
            return res
        res = None
        resmark = mark
        while True:
            memo[name] = res, p.mark
            p.mark = mark
            raw = method(p)
            if raw is None or p.mark <= resmark:
                break
            resmark = p.mark
            res = raw
        p.mark = resmark
        return res

    memoize_left_rec_wrapper.__name__ = name
    return memoize_left_rec_wrapper


def byte_offset_to_character_offset(line, col_offset):
    """_PyPegen_byte_offset_to_character_offset"""
    return tokenizer.byte_offset_to_character_offset_raw(line.encode("utf-8", "surrogatepass"), 0, col_offset)


class Parser:
    """The C struct Parser and the functions of pegen.c that take it."""

    KEYWORDS = {}
    SOFT_KEYWORDS = ()

    def __init__(self, tok, start_rule, flags, feature_version, source=None):
        """_PyPegen_Parser_New"""
        tok.type_comments = (flags & PyPARSE_TYPE_COMMENTS) > 0
        self.tok = tok
        self.tokens = []
        self.mark = 0
        self.fill = 0
        self.start_rule = start_rule
        self.parsing_started = False
        self.flags = flags
        self.feature_version = feature_version
        self.type_ignore_comments = []
        self.known_err_token = None
        self.call_invalid_rules = False
        self.last_stmt_location = (0, 0, 0, 0)
        self.source = source

    # Token filling (pegen.c)

    def fill_token(self):
        """_PyPegen_fill_token"""
        tok = self.tok
        new_token = tok.get()
        type = new_token.type
        # Record and skip '# type: ignore' comments
        while type == TYPE_IGNORE:
            self.type_ignore_comments.append((tok.lineno, new_token.string))
            new_token = tok.get()
            type = new_token.type

        # If we have reached the end and we are in single input mode we need
        # to insert a newline and reset the parsing
        if self.start_rule == SINGLE_INPUT and type == ENDMARKER and self.parsing_started:
            type = NEWLINE  # Add an extra newline
            self.parsing_started = False
            if tok.indent and not (self.flags & PyPARSE_DONT_IMPLY_DEDENT):
                tok.pendin = -tok.indent
                tok.indent = 0
        else:
            self.parsing_started = True
        self.initialize_token(new_token, type)

    def initialize_token(self, new_token, token_type):
        """initialize_token"""
        string = new_token.string
        type = self.KEYWORDS.get(string, NAME) if token_type == NAME else token_type
        self.tokens.append(Token(type, string, new_token.level, new_token.lineno, new_token.col_offset,
                                 new_token.end_lineno, new_token.end_col_offset, new_token.metadata))
        self.fill += 1
        if token_type == ERRORTOKEN:
            if self.tok.done == E_DECODE:
                self.raise_decode_error(self.tok.error)
            self.tokenizer_error()

    def get_last_nonnwhitespace_token(self):
        """_PyPegen_get_last_nonnwhitespace_token"""
        token = None
        for m in range(self.mark - 1, -1, -1):
            token = self.tokens[m]
            if token.type != ENDMARKER and (token.type < NEWLINE or token.type > DEDENT):
                break
        return token

    # Token matching (pegen.c)

    def lookahead(self, positive, func, *args):
        """_PyPegen_lookahead and its typed variants"""
        mark = self.mark
        res = func(*args)
        self.mark = mark
        return (res is not None) == positive

    def expect_token(self, type):
        """_PyPegen_expect_token"""
        mark = self.mark
        if mark == self.fill:
            self.fill_token()
        t = self.tokens[mark]
        if t.type != type:
            return None
        self.mark = mark + 1
        return t

    def expect_forced_result(self, result, expected):
        """_PyPegen_expect_forced_result"""
        if result is None:
            RAISE_SYNTAX_ERROR(self, "expected (%s)", expected)
        return result

    def expect_forced_token(self, type, expected):
        """_PyPegen_expect_forced_token"""
        mark = self.mark
        if mark == self.fill:
            self.fill_token()
        t = self.tokens[mark]
        if t.type != type:
            RAISE_SYNTAX_ERROR_KNOWN_LOCATION(self, t, "expected '%s'", expected)
        self.mark = mark + 1
        return t

    def expect_soft_keyword(self, keyword):
        """_PyPegen_expect_soft_keyword"""
        mark = self.mark
        if mark == self.fill:
            self.fill_token()
        t = self.tokens[mark]
        if t.type != NAME or t.string != keyword:
            return None
        return self.name_token()

    def name_from_token(self, t):
        """_PyPegen_name_from_token"""
        if t is None:
            return None
        return _ast.Name(new_identifier(self, t.string), LOAD, lineno=t.lineno, col_offset=t.col_offset,
                    end_lineno=t.end_lineno, end_col_offset=t.end_col_offset)

    def name_token(self):
        """_PyPegen_name_token"""
        return self.name_from_token(self.expect_token(NAME))

    def string_token(self):
        """_PyPegen_string_token"""
        return self.expect_token(STRING)

    def soft_keyword_token(self):
        """_PyPegen_soft_keyword_token"""
        t = self.expect_token(NAME)
        if t is None:
            return None
        if t.string in self.SOFT_KEYWORDS:
            return self.name_from_token(t)
        return None

    def number_token(self):
        """_PyPegen_number_token"""
        t = self.expect_token(NUMBER)
        if t is None:
            return None
        num_raw = t.string
        if self.feature_version < 6 and "_" in num_raw:
            RAISE_SYNTAX_ERROR(self, "Underscores in numeric literals are only supported "
                                     "in Python 3.6 and greater")
        try:
            c = parsenumber(num_raw)
        except ValueError as exc:
            # The only way a ValueError should happen in _this_ code is via
            # PyLong_FromString hitting a length limit.  Intentionally omitting
            # columns to avoid a wall of 1000s of '^'s on the error message.
            RAISE_ERROR_KNOWN_LOCATION(
                self, SyntaxError, t.lineno, -1, t.end_lineno, -1,
                "%s - Consider hexadecimal for huge integer literals "
                "to avoid decimal conversion limits.", exc)
        return _ast.Constant(c, None, lineno=t.lineno, col_offset=t.col_offset,
                        end_lineno=t.end_lineno, end_col_offset=t.end_col_offset)

    def dummy_name(self, *args):
        """_PyPegen_dummy_name"""
        return DUMMY_NAME

    def interactive_exit(self):
        """_PyPegen_interactive_exit"""
        return None

    # The parse (pegen.c)

    def parse(self):
        """_PyPegen_parse, from python.gram's trailer"""
        return getattr(self, START_RULES[self.start_rule])()

    def reset_parser_state_for_error_pass(self):
        self.last_stmt_location = (0, 0, 0, 0)
        for t in self.tokens:
            t.memo = {}
        self.mark = 0
        self.call_invalid_rules = True
        # Don't try to get extra tokens in interactive mode when trying to
        # raise specialized errors in the second pass.
        self.tok.interactive_underflow = 1  # IUNDERFLOW_STOP

    def is_end_of_source(self):
        return self.tok.done in (E_EOF, E_EOFS, E_EOLS)

    def run_parser(self):
        """_PyPegen_run_parser"""
        error = None
        try:
            res = self.parse()
        except SyntaxError as e:
            res, error = None, e
        except RecursionError:
            raise MemoryError(STACK_OVERFLOW) from None
        if res is None:
            if (self.flags & PyPARSE_ALLOW_INCOMPLETE_INPUT) and self.is_end_of_source():
                self.raise_error(IncompleteInputError, 0, "incomplete input")
            # Make a second parser pass. In this pass we activate heavier and
            # slower checks to produce better error messages and more complete
            # diagnostics. Extra "invalid_*" rules will be active during
            # parsing.  After a first-pass error the C parser's error
            # indicator stays set, so its second pass does nothing.
            last_token = self.tokens[self.fill - 1]
            if error is None:
                self.reset_parser_state_for_error_pass()
                try:
                    self.parse()
                except SyntaxError as e:
                    error = e
                except RecursionError:
                    raise MemoryError(STACK_OVERFLOW) from None
            # Set SyntaxErrors accordingly depending on the parser/tokenizer
            # status at the failure point.
            error = self.set_syntax_error(last_token, error)
            self.set_syntax_error_metadata(error)
            raise error

        if self.start_rule == SINGLE_INPUT and self.bad_single_statement():
            self.tok.done = E_BADSINGLE
            RAISE_SYNTAX_ERROR(self, "multiple statements found while compiling a single statement")
        return res

    def bad_single_statement(self):
        """Check that the source for a single input statement really is a
        single statement by looking at what is left in the buffer after
        parsing.  Trailing whitespace and comments are OK."""
        rest = self.tok.data[self.tok.cur:].split(b"\0", 1)[0]
        i, n = 0, len(rest)
        while True:
            while i < n and rest[i] in b" \t\n\014":
                i += 1
            if i == n:
                return False
            if rest[i] != ord("#"):
                return True
            # Suck up comment.
            while i < n and rest[i] != ord("\n"):
                i += 1

    # Errors (pegen_errors.c)

    def raise_unclosed_parentheses_error(self):
        tok = self.tok
        error_lineno = tok.parenlinenostack[tok.level - 1]
        error_col = tok.parencolstack[tok.level - 1]
        RAISE_ERROR_KNOWN_LOCATION(self, SyntaxError, error_lineno, error_col, error_lineno, -1,
                                   "'%c' was never closed", tok.parenstack[tok.level - 1])

    def tokenizer_error(self):
        """_Pypegen_tokenizer_error"""
        tok = self.tok
        if tok.error is not None:
            raise tok.error
        errtype = SyntaxError
        col_offset = -1
        done = tok.done
        if done == E_TOKEN:
            msg = "invalid token"
        elif done == E_EOF:
            if tok.level:
                self.raise_unclosed_parentheses_error()
            RAISE_SYNTAX_ERROR(self, "unexpected EOF while parsing")
        elif done == E_DEDENT:
            RAISE_INDENTATION_ERROR(self, "unindent does not match any outer indentation level")
        elif done == E_INTR:
            raise KeyboardInterrupt
        elif done == E_NOMEM:
            raise MemoryError
        elif done == E_TABSPACE:
            errtype = TabError
            msg = "inconsistent use of tabs and spaces in indentation"
        elif done == E_TOODEEP:
            errtype = IndentationError
            msg = "too many levels of indentation"
        elif done == E_LINECONT:
            col_offset = tok.cur - tok.buf - 1
            msg = "unexpected character after line continuation character"
        elif done == E_COLUMNOVERFLOW:
            raise OverflowError("Parser column offset overflow - source line is too big")
        else:
            msg = "unknown parsing error"
        RAISE_ERROR_KNOWN_LOCATION(self, errtype, tok.lineno, col_offset if col_offset >= 0 else 0,
                                   tok.lineno, -1, msg)

    def raise_decode_error(self, exc, a=None):
        """_Pypegen_raise_decode_error_known_location for the pending exception exc"""
        if isinstance(exc, UnicodeError):
            errtype = "unicode error"
        elif isinstance(exc, ValueError):
            errtype = "value error"
        else:
            raise exc
        if a is not None:
            RAISE_SYNTAX_ERROR_KNOWN_LOCATION(self, a, "(%s) %s", errtype, exc)
        RAISE_SYNTAX_ERROR(self, "(%s) %s", errtype, exc)

    def tokenize_full_source_to_check_for_errors(self, error):
        """_PyPegen_tokenize_full_source_to_check_for_errors: the error to report.

        Tokenize the whole input to see if there are any tokenization errors
        such as mismatching parentheses. These get priority over generic
        syntax errors only if the line number of the error is before the one
        that we had for the generic error."""
        tok = self.tok
        # We don't want to tokenize to the end for interactive input
        if tok.prompt is not None:
            return error
        current_token = self.known_err_token if self.known_err_token is not None else self.tokens[self.fill - 1]
        current_err_line = current_token.lineno
        new_error = None
        try:
            while True:
                type = tok.get().type
                if type == ERRORTOKEN:
                    if tok.error is not None:
                        raise tok.error
                    if tok.level != 0:
                        error_lineno = tok.parenlinenostack[tok.level - 1]
                        if current_err_line > error_lineno:
                            self.raise_unclosed_parentheses_error()
                    break
                if type == ENDMARKER:
                    break
        except SyntaxError as e:
            new_error = e
        # If we're in an f-string, we want the syntax error in the expression
        # part to propagate, so that tokenizer errors (like expecting '}')
        # that happen afterwards do not swallow it.
        if new_error is not None and tok.tok_mode_stack_index <= 0:
            return new_error
        return error

    def raise_error(self, errtype, use_mark, errmsg, *args):
        """_PyPegen_raise_error"""
        if self.fill == 0:
            self.raise_error_known_location(errtype, 0, 0, 0, -1, errmsg, *args)
        if use_mark and self.mark == self.fill:
            self.fill_token()
        if self.known_err_token is not None:
            t = self.known_err_token
        else:
            t = self.tokens[self.mark if use_mark else self.fill - 1]
        end_col_offset = -1
        if t.col_offset == -1:
            tok = self.tok
            if tok.cur == tok.buf:
                col_offset = 0
            else:
                col_offset = tok.cur - tok.line_start
        else:
            col_offset = t.col_offset + 1
        if t.end_col_offset != -1:
            end_col_offset = t.end_col_offset + 1
        self.raise_error_known_location(errtype, t.lineno, col_offset, t.end_lineno, end_col_offset,
                                        errmsg, *args)

    def raise_error_known_location(self, errtype, lineno, col_offset, end_lineno, end_col_offset,
                                   errmsg, *args):
        """_PyPegen_raise_error_known_location"""
        tok = self.tok
        if end_lineno == CURRENT_POS:
            end_lineno = tok.lineno
        if end_col_offset == CURRENT_POS:
            end_col_offset = tok.cur - tok.line_start
        errstr = errmsg % args if args else errmsg

        error_line = None
        if self.start_rule == FILE_INPUT:
            error_line = program_text(tok.filename, lineno, tok.encoding)
        if error_line is None:
            if tok.lineno <= lineno and tok.inp > tok.buf:
                error_line = tok.current_line()
            else:
                error_line = self.error_line_from_source(lineno)

        col_number = byte_offset_to_character_offset(error_line, col_offset)
        end_col_number = end_col_offset
        if end_col_offset > 0:
            end_col_number = byte_offset_to_character_offset(error_line, end_col_offset)
        raise errtype(errstr, (tok.filename, lineno, col_number, error_line, end_lineno, end_col_number))

    def error_line_from_source(self, lineno):
        """get_error_line_from_tokenizer_buffers, for string input"""
        data = self.tok.str.split(b"\0", 1)[0]
        start = 0
        for _ in range(lineno - 1):
            newline = data.find(b"\n", start)
            if newline < 0 or newline + 1 > len(data):
                break
            start = newline + 1
        end = data.find(b"\n", start)
        return data[start:end if end >= 0 else len(data)].decode("utf-8", "replace")

    def set_syntax_error(self, last_token, error):
        """_Pypegen_set_syntax_error: the error to report."""
        tok = self.tok
        # Existing syntax error
        if error is not None:
            # Prioritize tokenizer errors to custom syntax errors raised on the
            # second phase only if the errors come from the parser.
            if tok.done in (E_DONE, E_OK):
                error = self.tokenize_full_source_to_check_for_errors(error)
            # Propagate the existing syntax error.
            return error
        try:
            # Initialization error
            if self.fill == 0:
                RAISE_SYNTAX_ERROR(self, "error at start before reading any input")
            # Parser encountered EOF (End of File) unexpectedtly
            if last_token.type == ERRORTOKEN and tok.done == E_EOF:
                if tok.level:
                    self.raise_unclosed_parentheses_error()
                RAISE_SYNTAX_ERROR(self, "unexpected EOF while parsing")
            # Indentation error in the tokenizer
            if last_token.type == INDENT or last_token.type == DEDENT:
                RAISE_INDENTATION_ERROR(self, "unexpected indent" if last_token.type == INDENT
                                        else "unexpected unindent")
            # Unknown error (generic case)

            # Use the last token we found on the first pass to avoid reporting
            # incorrect locations for generic syntax errors just because we
            # reached further away when trying to find specific syntax errors
            # in the second pass.
            RAISE_SYNTAX_ERROR_KNOWN_LOCATION(self, last_token, "invalid syntax")
        except SyntaxError as e:
            error = e
        if not isinstance(error, IndentationError) and error.msg == "invalid syntax":
            # _PyPegen_tokenize_full_source_to_check_for_errors will override
            # the existing generic SyntaxError we just raised if errors are
            # found.
            error = self.tokenize_full_source_to_check_for_errors(error)
        return error

    def set_syntax_error_metadata(self, error):
        """_PyPegen_set_syntax_error_metadata"""
        if not isinstance(error, SyntaxError):
            return
        source = self.tok.str
        if source is not None:
            try:
                source = source.split(b"\0", 1)[0].decode(self.tok.encoding or "utf-8")
            except (UnicodeError, LookupError):
                source = None
        error._metadata = (self.last_stmt_location[0], self.last_stmt_location[1], source)


def new_identifier(p, n):
    """_PyPegen_new_identifier"""
    # Check whether there are non-ASCII characters in the identifier; if so,
    # normalize to NFKC.
    if not n.isascii():
        import unicodedata
        n = unicodedata.normalize("NFKC", n)
    if n in ("None", "True", "False"):
        raise ValueError(f"identifier field can't represent '{n}' constant")
    return sys.intern(n)


def program_text(filename, lineno, encoding):
    """_PyErr_ProgramDecodedTextObject: line lineno of the file, or None."""
    if not isinstance(filename, str) or lineno <= 0:
        return None
    try:
        with open(filename, "rb") as f:
            for i, line in enumerate(f, 1):
                if i == lineno:
                    return line.decode(encoding or "utf-8", "replace")
    except (OSError, ValueError):
        return None
    return None


def parsenumber_raw(s):
    """parsenumber_raw: the value of a NUMBER token without underscores."""
    if s[-1] in "jJ":
        return complex(0.0, float(s[:-1]))
    if s[0] == "0" and len(s) > 1 and s[1] in "xXoObB":
        return int(s, 0)
    try:
        return int(s, 10) if s.isdigit() else float(s)
    except ValueError:
        if s.isdigit():
            raise
        return float(s)


def parsenumber(s):
    """parsenumber"""
    return parsenumber_raw(s.replace("_", "") if "_" in s else s)


# pegen.h's error macros

def RAISE_ERROR_KNOWN_LOCATION(p, errtype, lineno, col_offset, end_lineno, end_col_offset, errmsg, *args):
    col = CURRENT_POS if col_offset == CURRENT_POS else col_offset + 1
    end_col = CURRENT_POS if end_col_offset == CURRENT_POS else end_col_offset + 1
    p.raise_error_known_location(errtype, lineno, col, end_lineno, end_col, errmsg, *args)


def RAISE_SYNTAX_ERROR(p, msg, *args):
    p.raise_error(SyntaxError, 0, msg, *args)


def RAISE_INDENTATION_ERROR(p, msg, *args):
    p.raise_error(IndentationError, 0, msg, *args)


def RAISE_SYNTAX_ERROR_ON_NEXT_TOKEN(p, msg, *args):
    p.raise_error(SyntaxError, 1, msg, *args)


def RAISE_SYNTAX_ERROR_KNOWN_RANGE(p, a, b, msg, *args):
    RAISE_ERROR_KNOWN_LOCATION(p, SyntaxError, a.lineno, a.col_offset, b.end_lineno, b.end_col_offset, msg, *args)


def RAISE_SYNTAX_ERROR_KNOWN_LOCATION(p, a, msg, *args):
    RAISE_ERROR_KNOWN_LOCATION(p, SyntaxError, a.lineno, a.col_offset, a.end_lineno, a.end_col_offset, msg, *args)


def RAISE_SYNTAX_ERROR_STARTING_FROM(p, a, msg, *args):
    RAISE_ERROR_KNOWN_LOCATION(p, SyntaxError, a.lineno, a.col_offset, CURRENT_POS, CURRENT_POS, msg, *args)


def compute_parser_flags(flags):
    parser_flags = 0
    if flags & PyCF_DONT_IMPLY_DEDENT:
        parser_flags |= PyPARSE_DONT_IMPLY_DEDENT
    if flags & PyCF_IGNORE_COOKIE:
        parser_flags |= PyPARSE_IGNORE_COOKIE
    if flags & CO_FUTURE_BARRY_AS_BDFL:
        parser_flags |= PyPARSE_BARRY_AS_BDFL
    if flags & PyCF_TYPE_COMMENTS:
        parser_flags |= PyPARSE_TYPE_COMMENTS
    if flags & PyCF_ALLOW_INCOMPLETE_INPUT:
        parser_flags |= PyPARSE_ALLOW_INCOMPLETE_INPUT
    return parser_flags


def run_parser_from_string(parser_class, source, filename, mode, flags=0, feature_version=-1):
    """_PyParser_ASTFromString and _PyPegen_run_parser_from_string.

    A str source is taken as already decoded, as builtins.compile() passes it
    with PyCF_IGNORE_COOKIE; bytes are decoded from their BOM or coding
    cookie unless flags has PyCF_IGNORE_COOKIE.
    """
    start_rule = MODES[mode]
    if isinstance(source, str):
        flags |= PyCF_IGNORE_COOKIE
        source = source.encode("utf-8", "surrogatepass")
    exec_input = start_rule == FILE_INPUT
    parser_flags = compute_parser_flags(flags)
    try:
        if flags & PyCF_IGNORE_COOKIE:
            tok = tokenizer.from_utf8(source, exec_input)
        else:
            tok = tokenizer.from_string(source, exec_input)
    except Exception as error:
        raise tokenizer.raise_init_error(error, filename) from None
    tok.filename = filename
    tok.barry_as_bdfl = bool(parser_flags & PyPARSE_BARRY_AS_BDFL)
    if not (flags & PyCF_ONLY_AST) or feature_version < 0:
        feature_version = PY_MINOR_VERSION
    p = parser_class(tok, start_rule, parser_flags, feature_version, source)
    return p.run_parser()


