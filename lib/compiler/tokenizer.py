"""CPython 3.14's tokenizer: Parser/lexer/{lexer,state,buffer}.c, the string,
UTF-8 and readline front ends and helpers of Parser/tokenizer/, and the
_tokenize module (Python/Python-tokenize.c).

Parser interface (what pegen.c uses of struct tok_state):

  from_string(source: bytes, exec_input, preserve_crlf=False) -> Tokenizer
      _PyTokenizer_FromString: honours a BOM and a PEP 263 coding cookie.
  from_utf8(source: bytes, exec_input, preserve_crlf=False) -> Tokenizer
      _PyTokenizer_FromUTF8 (PyCF_IGNORE_COOKIE: str sources, UTF-8 encoded).
  Both raise what the C reports as an init failure (SyntaxError,
  UnicodeDecodeError, LookupError); apply raise_init_error(error, filename)
  like _PyTokenizer_raise_init_error.  exec_input is true for file input and
  false for 'single'/'eval' input, as in _PyPegen_run_parser_from_string.

  tok.get() -> Token          _PyTokenizer_Get
      Token fields: type (the C token number, same as the token module),
      string (the token text), start/end (byte offsets into tok.data, or
      None), lineno, col_offset, end_lineno, end_col_offset (columns in
      UTF-8 bytes from the line start, -1 when unset), level, metadata (the
      f-string/t-string debug or interpolation expression text, or None).
      On ERRORTOKEN the exception the C would have set (PyErr_Occurred) is
      in tok.error, else tok.error is None and tok.done holds the E_* code.

  tok_state fields kept under their C names: done, lineno, level, parenstack,
  parenlinenostack, parencolstack, indent, pendin, prompt, nextprompt,
  interactive_underflow, type_comments, barry_as_bdfl, filename, encoding,
  report_warnings, tok_mode_stack_index, fp_interactive, str (the decoded
  UTF-8 source), and the buffer positions buf, cur, inp, line_start,
  multi_line_start as byte offsets into tok.data.  tok.current_line() is the
  text from line_start to inp, as the C error paths decode it.

TokenizerIter(readline, *, extra_tokens, encoding=None) is
_tokenize.TokenizerIter.
"""
import warnings
from token import (ENDMARKER, NAME, NUMBER, STRING, NEWLINE, INDENT, DEDENT, OP, COMMENT, NL, ERRORTOKEN,
                   TYPE_IGNORE, TYPE_COMMENT, FSTRING_START, FSTRING_MIDDLE, FSTRING_END, TSTRING_START,
                   TSTRING_MIDDLE, TSTRING_END, ELLIPSIS, DOT, NOTEQUAL, EXACT_TOKEN_TYPES)

# Include/errcode.h
E_OK = 10
E_EOF = 11
E_INTR = 12
E_TOKEN = 13
E_SYNTAX = 14
E_NOMEM = 15
E_DONE = 16
E_ERROR = 17
E_TABSPACE = 18
E_OVERFLOW = 19
E_TOODEEP = 20
E_DEDENT = 21
E_DECODE = 22
E_EOFS = 23
E_EOLS = 24
E_LINECONT = 25
E_BADSINGLE = 27
E_INTERACT_STOP = 28
E_COLUMNOVERFLOW = 29

EOF = -1
MAXINDENT = 100
MAXLEVEL = 200
MAXFSTRINGLEVEL = 150
MAX_EXPR_NESTING = 3
TABSIZE = 8
ALTTABSIZE = 1

STATE_INIT, STATE_SEEK_CODING, STATE_NORMAL = range(3)
IUNDERFLOW_NORMAL, IUNDERFLOW_STOP = range(2)
TOK_REGULAR_MODE, TOK_FSTRING_MODE = range(2)
FSTRING, TSTRING = range(2)

ISSTRINGLIT = frozenset((STRING, FSTRING_MIDDLE, TSTRING_MIDDLE))

# Parser/token.c: _PyToken_OneChar, _PyToken_TwoChars, _PyToken_ThreeChars
_EXACT = {tuple(map(ord, s)): t for s, t in EXACT_TOKEN_TYPES.items()}
_EXACT[(ord('<'), ord('>'))] = NOTEQUAL


def _PyToken_OneChar(c):
    return _EXACT.get((c,), OP)


def _PyToken_TwoChars(c1, c2):
    return _EXACT.get((c1, c2), OP)


def _PyToken_ThreeChars(c1, c2, c3):
    return _EXACT.get((c1, c2, c3), OP)


_O = ord
_SPACE, _TAB, _FF, _NL, _CR, _BSL, _HASH = map(ord, ' \t\f\n\r\\#')
_SQ, _DQ, _LBRACE, _RBRACE = map(ord, '\'"{}')


def is_potential_identifier_start(c):
    return 97 <= c <= 122 or 65 <= c <= 90 or c == 95 or c >= 128


def is_potential_identifier_char(c):
    return 97 <= c <= 122 or 65 <= c <= 90 or 48 <= c <= 57 or c == 95 or c >= 128


def isdigit(c):
    return 48 <= c <= 57


def isxdigit(c):
    return 48 <= c <= 57 or 97 <= c <= 102 or 65 <= c <= 70


def isalnum(c):
    return 48 <= c <= 57 or 97 <= c <= 122 or 65 <= c <= 90


def tolower(c):
    return c + 32 if 65 <= c <= 90 else c


def _at(data, i):
    """data[i], or 0 past the end, like reading a NUL-terminated C buffer."""
    return data[i] if 0 <= i < len(data) else 0


class Token:
    """struct token, plus the type _PyTokenizer_Get returns and the text."""
    __slots__ = ("type", "string", "level", "lineno", "col_offset", "end_lineno", "end_col_offset",
                 "start", "end", "metadata")

    def __init__(self):
        self.metadata = None

    def __repr__(self):
        return (f"Token({self.type}, {self.string!r}, {self.lineno}:{self.col_offset}-"
                f"{self.end_lineno}:{self.end_col_offset})")


class TokenizerMode:
    """tokenizer_mode."""
    __slots__ = ("kind", "curly_bracket_depth", "curly_bracket_expr_start_depth", "quote", "quote_size",
                 "raw", "start", "multi_line_start", "first_line", "last_expr_start", "in_debug",
                 "in_format_spec", "string_kind")

    def __init__(self, kind):
        self.kind = kind
        self.quote = 0
        self.quote_size = 0
        self.in_debug = 0
        self.in_format_spec = 0
        self.curly_bracket_depth = 0
        self.curly_bracket_expr_start_depth = -1
        self.string_kind = FSTRING
        self.last_expr_start = None


def INSIDE_FSTRING_EXPR(mode):
    return mode.curly_bracket_expr_start_depth >= 0


def INSIDE_FSTRING_EXPR_AT_TOP(mode):
    return mode.curly_bracket_depth - mode.curly_bracket_expr_start_depth == 1


class Tokenizer:
    """struct tok_state and the lexer (Parser/lexer/lexer.c)."""

    def __init__(self):
        # _PyTokenizer_tok_new
        self.data = b""
        self.buf = self.cur = self.inp = 0
        self.fp_interactive = 0
        self.interactive_src_start = None
        self.interactive_src_end = None
        self.start = None
        self.done = E_OK
        self.error = None
        self.fp = None
        self.input = None
        self.tabsize = TABSIZE
        self.indent = 0
        self.indstack = [0] * MAXINDENT
        self.atbol = 1
        self.pendin = 0
        self.prompt = self.nextprompt = None
        self.lineno = 0
        self.first_lineno = 0
        self.starting_col_offset = -1
        self.col_offset = -1
        self.level = 0
        self.parenstack = [0] * MAXLEVEL
        self.parenlinenostack = [0] * MAXLEVEL
        self.parencolstack = [0] * MAXLEVEL
        self.altindstack = [0] * MAXINDENT
        self.decoding_state = STATE_INIT
        self.decoding_erred = 0
        self.enc = None
        self.encoding = None
        self.cont_line = 0
        self.line_start = 0
        self.multi_line_start = 0
        self.filename = None
        self.readline = None
        self.type_comments = 0
        self.interactive_underflow = IUNDERFLOW_NORMAL
        self.underflow = None
        self.str = None
        self.report_warnings = 1
        self.tok_extra_tokens = 0
        self.comment_newline = 0
        self.implicit_newline = 0
        self.tok_mode_stack = [TokenizerMode(TOK_REGULAR_MODE)]
        self.tok_mode_stack_index = 0
        self.barry_as_bdfl = 0

    def current_line(self):
        return self.data[self.line_start:self.inp].decode("utf-8", "replace")

    # ---- state.c

    def token_setup(self, token, type, start, end):
        token.level = self.level
        token.lineno = self.first_lineno if type in ISSTRINGLIT else self.lineno
        token.end_lineno = self.lineno
        token.col_offset = token.end_col_offset = -1
        token.start = start
        token.end = end
        if start is not None and end is not None:
            token.col_offset = self.starting_col_offset
            token.end_col_offset = self.col_offset
        return type

    def type_comment_token_setup(self, token, type, col_offset, end_col_offset, start, end):
        token.level = self.level
        token.lineno = token.end_lineno = self.lineno
        token.col_offset = col_offset
        token.end_col_offset = end_col_offset
        token.start = start
        token.end = end
        return type

    # ---- helpers.c: errors

    def _syntaxerror_range(self, msg, col_offset, end_col_offset):
        if self.done == E_ERROR:
            return ERRORTOKEN
        data, line_start = self.data, self.line_start
        errtext = data[line_start:self.cur].decode("utf-8", "replace")
        if col_offset == -1:
            col_offset = len(errtext)
        elif col_offset > 0:
            col_offset = byte_col_to_char_col(data, line_start, col_offset)
        if end_col_offset == -1:
            end_col_offset = col_offset
        elif end_col_offset > 0:
            end_col_offset = byte_col_to_char_col(data, line_start, end_col_offset)
        line_end = len(data)      # strcspn(line_start, "\n") in a NUL-terminated buffer
        for stop in (b"\n", b"\0"):
            found = data.find(stop, line_start)
            if 0 <= found < line_end:
                line_end = found
        if line_end != self.cur:
            errtext = data[line_start:line_end].decode("utf-8", "replace")
        self.error = SyntaxError(msg, (self.filename, self.lineno, col_offset, errtext,
                                       self.lineno, end_col_offset))
        self.done = E_ERROR
        return ERRORTOKEN

    def syntaxerror(self, msg):
        return self._syntaxerror_range(msg, -1, -1)

    def syntaxerror_known_range(self, col_offset, end_col_offset, msg):
        return self._syntaxerror_range(msg, col_offset, end_col_offset)

    def indenterror(self):
        self.done = E_TABSPACE
        self.cur = self.inp
        return ERRORTOKEN

    def error_ret(self):
        self.decoding_erred = 1
        self.data = b""
        self.buf = self.cur = self.inp = 0
        self.start = None
        self.done = E_DECODE
        return None

    def _warn(self, category, msg):
        """PyErr_WarnExplicitObject; the exception it raises, if any."""
        try:
            warnings.warn_explicit(msg, category, self.filename, self.lineno)
        except Exception as error:
            return error
        return None

    def warn_invalid_escape_sequence(self, first_invalid_escape_char):
        if not self.report_warnings:
            return 0
        ch = chr(first_invalid_escape_char)
        error = self._warn(SyntaxWarning, f'"\\{ch}" is an invalid escape sequence. '
                           f'Such sequences will not work in the future. '
                           f'Did you mean "\\\\{ch}"? A raw string is also an option.')
        if error is not None:
            if isinstance(error, SyntaxWarning):
                return self.syntaxerror(f'"\\{ch}" is an invalid escape sequence. '
                                        f'Did you mean "\\\\{ch}"? A raw string is also an option.')
            self.error = error
            return -1
        return 0

    def parser_warn(self, category, msg):
        if not self.report_warnings:
            return 0
        error = self._warn(category, msg)
        if error is None:
            return 0
        if isinstance(error, category):
            # Replace the warning exception with a SyntaxError to get a more
            # accurate error report.
            self.syntaxerror(msg)
        else:
            self.error = error
        self.done = E_ERROR
        return -1

    # ---- lexer.c

    def tok_nextc(self):
        """Get next char, updating state; error code goes into self.done."""
        while True:
            if self.cur != self.inp:
                self.col_offset += 1
                c = self.data[self.cur]
                self.cur += 1
                return c
            if self.done != E_OK:
                return EOF
            rc = self.underflow(self)
            if not rc:
                self.cur = self.inp
                return EOF
            self.line_start = self.cur
            if self.data.find(0, self.line_start, self.inp) >= 0:
                self.syntaxerror("source code cannot contain null bytes")
                self.cur = self.inp
                return EOF

    def tok_backup(self, c):
        """Back up one character."""
        if c != EOF:
            self.cur -= 1
            if self.cur < self.buf:
                raise SystemError("tokenizer beginning of buffer")
            self.col_offset -= 1

    def set_ftstring_expr(self, token, c):
        tok_mode = self.tok_mode_stack[self.tok_mode_stack_index]
        if not (tok_mode.in_debug or tok_mode.string_kind == TSTRING) or token.metadata is not None:
            return 0
        expression = self.data[tok_mode.last_expr_start:self.start]
        # Look for a # character outside of string literals.
        hash_detected = 0
        in_string = 0
        quote_char = 0
        if 35 in expression:
            i = 0
            n = len(expression)
            while i < n:
                ch = expression[i]
                if ch == _BSL:      # skip escaped characters
                    i += 2
                    continue
                if ch == _DQ or ch == _SQ:
                    # STRING tokens hold an odd number of quotes and the lexer
                    # only gets here with valid STRING tokens.
                    if not in_string:
                        in_string = 1
                        quote_char = ch
                    elif ch == quote_char:
                        in_string = 0
                elif ch == _HASH and not in_string:
                    hash_detected = 1
                    break
                i += 1
        if hash_detected:
            result = bytearray()
            i = 0
            n = len(expression)
            in_string = 0
            quote_char = 0
            while i < n:
                ch = expression[i]
                if ch == _BSL:
                    # Copy escaped characters without interpreting them as
                    # quotes or comment markers.
                    result.append(ch)
                    i += 1
                    if i < n:
                        result.append(expression[i])
                elif ch == _DQ or ch == _SQ:
                    if not in_string:
                        in_string = 1
                        quote_char = ch
                    elif ch == quote_char:
                        in_string = 0
                    result.append(ch)
                elif ch == _HASH and not in_string:      # skip comments
                    while i < n and expression[i] != _NL:
                        i += 1
                    if i < n:
                        result.append(_NL)
                else:
                    result.append(ch)
                i += 1
            expression = result
        try:
            token.metadata = bytes(expression).decode("utf-8")
        except UnicodeDecodeError as error:
            self.error = error
            return -1
        return 0

    def lookahead(self, test):
        s = 0
        while True:
            c = self.tok_nextc()
            if s == len(test):
                res = not is_potential_identifier_char(c)
            elif c == ord(test[s]):
                s += 1
                continue
            else:
                res = 0
            self.tok_backup(c)
            while s:
                s -= 1
                self.tok_backup(ord(test[s]))
            return res

    def verify_end_of_number(self, c, kind):
        if self.tok_extra_tokens:
            # Extra tokens are a bit more liberal: no warnings about invalid
            # literals.
            return 1
        # Emit a deprecation warning only if the numeric literal is immediately
        # followed by one of keywords which can occur after a numeric literal
        # in valid code: "and", "else", "for", "if", "in", "is" and "or".  It
        # allows gradually deprecating existing valid code without adding a
        # warning before an error in most cases of invalid numeric literal.
        # Raise a syntax error with a slightly better message than plain
        # "invalid syntax" if the literal is followed by another keyword or
        # identifier.
        r = 0
        if c == 97:         # a
            r = self.lookahead("nd")
        elif c == 101:      # e
            r = self.lookahead("lse")
        elif c == 102:      # f
            r = self.lookahead("or")
        elif c == 105:      # i
            c2 = self.tok_nextc()
            if c2 in (102, 110, 115):
                r = 1
            self.tok_backup(c2)
        elif c == 111:      # o
            r = self.lookahead("r")
        elif c == 110:      # n
            r = self.lookahead("ot")
        if r:
            self.tok_backup(c)
            if self.parser_warn(SyntaxWarning, f"invalid {kind} literal"):
                return 0
            self.tok_nextc()
        elif 0 <= c < 128 and is_potential_identifier_char(c):
            self.tok_backup(c)
            self.syntaxerror(f"invalid {kind} literal")
            return 0
        return 1

    def verify_identifier(self):
        """Verify that the identifier follows PEP 3131."""
        if self.tok_extra_tokens:
            return 1
        if self.decoding_erred:
            return 0
        raw = bytes(self.data[self.start:self.cur])
        try:
            s = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            self.error = error
            self.done = E_DECODE
            return 0
        invalid = scan_identifier(s)
        if invalid < len(s):
            ch = s[invalid]
            if invalid + 1 < len(s):
                # Determine the offset in UTF-8 encoded input.
                self.cur = self.start + len(s[:invalid + 1].encode("utf-8"))
            if ch.isprintable():
                self.syntaxerror(f"invalid character '{ch}' (U+{ord(ch):04X})")
            else:
                self.syntaxerror(f"invalid non-printable character U+{ord(ch):04X}")
            return 0
        return 1

    def tok_decimal_tail(self):
        while True:
            c = self.tok_nextc()
            while isdigit(c):
                c = self.tok_nextc()
            if c != 95:
                break
            c = self.tok_nextc()
            if not isdigit(c):
                self.tok_backup(c)
                self.syntaxerror("invalid decimal literal")
                return 0
        return c

    def tok_continuation_line(self):
        c = self.tok_nextc()
        if c == _CR:
            c = self.tok_nextc()
        if c != _NL:
            self.done = E_LINECONT
            return -1
        c = self.tok_nextc()
        if c == EOF:
            self.done = E_EOF
            self.cur = self.inp
            return -1
        self.tok_backup(c)
        return c

    def maybe_raise_syntax_error_for_string_prefixes(self, saw_b, saw_r, saw_u, saw_f, saw_t):
        # Supported: rb, rf, rt (in any order)
        # Unsupported: ub, ur, uf, ut, bf, bt, ft (in any order)
        for (a, sa), (b, sb) in ((("u", saw_u), ("b", saw_b)), (("u", saw_u), ("r", saw_r)),
                                 (("u", saw_u), ("f", saw_f)), (("u", saw_u), ("t", saw_t)),
                                 (("b", saw_b), ("f", saw_f)), (("b", saw_b), ("t", saw_t)),
                                 (("f", saw_f), ("t", saw_t))):
            if sa and sb:
                self.syntaxerror_known_range(self.start + 1 - self.line_start, self.cur - self.line_start,
                                             f"'{a}' and '{b}' prefixes are incompatible")
                return -1
        return 0

    def string_prefix(self):
        """TOK_GET_STRING_PREFIX."""
        return "t" if self.tok_mode_stack[self.tok_mode_stack_index].string_kind == TSTRING else "f"

    def tok_get_normal_mode(self, current_tok, token):
        p_start = p_end = None
        while True:     # nextline
            self.start = None
            self.starting_col_offset = -1
            blankline = 0

            # Get indentation level
            if self.atbol:
                col = 0
                altcol = 0
                self.atbol = 0
                cont_line_col = 0
                while True:
                    c = self.tok_nextc()
                    if c == _SPACE:
                        col += 1
                        altcol += 1
                    elif c == _TAB:
                        col = (col // self.tabsize + 1) * self.tabsize
                        altcol = (altcol // ALTTABSIZE + 1) * ALTTABSIZE
                    elif c == _FF:      # Control-L (formfeed), for Emacs users
                        col = altcol = 0
                    elif c == _BSL:
                        # Indentation cannot be split over multiple physical
                        # lines using backslashes: the first backslash
                        # preceded by whitespace determines the indentation of
                        # whatever comes next.
                        cont_line_col = cont_line_col if cont_line_col else col
                        c = self.tok_continuation_line()
                        if c == -1:
                            return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                    elif c == EOF and self.error is not None:
                        return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                    else:
                        break
                self.tok_backup(c)
                if c == _HASH or c == _NL or c == _CR:
                    # Lines with only whitespace and/or comments shouldn't
                    # affect the indentation and are not passed to the parser
                    # as NEWLINE tokens, except *totally* empty lines in
                    # interactive mode, which signal the end of a command group.
                    if col == 0 and c == _NL and self.prompt is not None:
                        blankline = 0       # Let it through
                    elif self.prompt is not None and self.lineno == 1:
                        # In interactive mode, if the first line contains only
                        # spaces and/or a comment, let it through.
                        blankline = 0
                        col = altcol = 0
                    else:
                        blankline = 1       # Ignore completely
                    # We can't jump back right here since we still may need
                    # to skip to the end of a comment.
                if not blankline and self.level == 0:
                    col = cont_line_col if cont_line_col else col
                    altcol = cont_line_col if cont_line_col else altcol
                    if col == self.indstack[self.indent]:
                        # No change
                        if altcol != self.altindstack[self.indent]:
                            return self.token_setup(token, self.indenterror(), p_start, p_end)
                    elif col > self.indstack[self.indent]:
                        # Indent -- always one
                        if self.indent + 1 >= MAXINDENT:
                            self.done = E_TOODEEP
                            self.cur = self.inp
                            return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                        if altcol <= self.altindstack[self.indent]:
                            return self.token_setup(token, self.indenterror(), p_start, p_end)
                        self.pendin += 1
                        self.indent += 1
                        self.indstack[self.indent] = col
                        self.altindstack[self.indent] = altcol
                    else:
                        # Dedent -- any number, must be consistent
                        while self.indent > 0 and col < self.indstack[self.indent]:
                            self.pendin -= 1
                            self.indent -= 1
                        if col != self.indstack[self.indent]:
                            self.done = E_DEDENT
                            self.cur = self.inp
                            return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                        if altcol != self.altindstack[self.indent]:
                            return self.token_setup(token, self.indenterror(), p_start, p_end)

            self.start = self.cur
            self.starting_col_offset = self.col_offset

            # Return pending indents/dedents
            if self.pendin != 0:
                if self.pendin < 0:
                    if self.tok_extra_tokens:
                        p_start = self.cur
                        p_end = self.cur
                    self.pendin += 1
                    return self.token_setup(token, DEDENT, p_start, p_end)
                else:
                    if self.tok_extra_tokens:
                        p_start = self.buf
                        p_end = self.cur
                    self.pendin -= 1
                    return self.token_setup(token, INDENT, p_start, p_end)

            # Peek ahead at the next character
            c = self.tok_nextc()
            self.tok_backup(c)

            while True:     # again
                self.start = None
                # Skip spaces
                c = self.tok_nextc()
                while c == _SPACE or c == _TAB or c == _FF:
                    c = self.tok_nextc()

                # Set start of current token
                self.start = self.cur - 1
                self.starting_col_offset = self.col_offset - 1

                # Skip comment, unless it's a type comment
                if c == _HASH:
                    p = None
                    while c != EOF and c != _NL and c != _CR:
                        c = self.tok_nextc()
                    if self.tok_extra_tokens:
                        p = self.start
                    if self.type_comments:
                        result, p = self.type_comment(token, c, blankline)
                        if result is not None:
                            return result
                    if self.tok_extra_tokens:
                        self.tok_backup(c)      # don't eat the newline or EOF
                        p_start = p
                        p_end = self.cur
                        self.comment_newline = blankline
                        return self.token_setup(token, COMMENT, p_start, p_end)

                if self.done == E_INTERACT_STOP:
                    return self.token_setup(token, ENDMARKER, p_start, p_end)

                # Check for EOF and errors now
                if c == EOF:
                    if self.level:
                        return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                    return self.token_setup(token, ENDMARKER if self.done == E_EOF else ERRORTOKEN,
                                            p_start, p_end)

                # Identifier (most frequent token!)
                nonascii = 0
                if is_potential_identifier_start(c):
                    # Process the various legal combinations of b"", r"", u"", and f"".
                    saw_b = saw_r = saw_u = saw_f = saw_t = 0
                    while True:
                        if not saw_b and (c == 98 or c == 66):
                            saw_b = 1
                        # Since this is a backwards compatibility support
                        # literal we don't want to support it in arbitrary
                        # order like byte literals.
                        elif not saw_u and (c == 117 or c == 85):
                            saw_u = 1
                        # ur"" and ru"" are not supported
                        elif not saw_r and (c == 114 or c == 82):
                            saw_r = 1
                        elif not saw_f and (c == 102 or c == 70):
                            saw_f = 1
                        elif not saw_t and (c == 116 or c == 84):
                            saw_t = 1
                        else:
                            break
                        c = self.tok_nextc()
                        if c == _DQ or c == _SQ:
                            # Raise error on incompatible string prefixes
                            if self.maybe_raise_syntax_error_for_string_prefixes(
                                    saw_b, saw_r, saw_u, saw_f, saw_t) < 0:
                                return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                            # Handle valid f or t string creation
                            if saw_f or saw_t:
                                return self.f_string_quote(token, c)
                            return self.letter_quote(token, c)
                    while is_potential_identifier_char(c):
                        if c >= 128:
                            nonascii = 1
                        c = self.tok_nextc()
                    self.tok_backup(c)
                    if nonascii and not self.verify_identifier():
                        return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                    return self.token_setup(token, NAME, self.start, self.cur)

                if c == _CR:
                    c = self.tok_nextc()

                # Newline
                if c == _NL:
                    self.atbol = 1
                    if blankline or self.level > 0:
                        if self.tok_extra_tokens:
                            if self.comment_newline:
                                self.comment_newline = 0
                            return self.token_setup(token, NL, self.start, self.cur)
                        break       # goto nextline
                    if self.comment_newline and self.tok_extra_tokens:
                        self.comment_newline = 0
                        return self.token_setup(token, NL, self.start, self.cur)
                    p_start = self.start
                    p_end = self.cur - 1        # Leave '\n' out of the string
                    self.cont_line = 0
                    return self.token_setup(token, NEWLINE, p_start, p_end)

                # Period or number starting with period?
                if c == 46:
                    c = self.tok_nextc()
                    if isdigit(c):
                        return self.tok_fraction(token, c)
                    elif c == 46:
                        c = self.tok_nextc()
                        if c == 46:
                            return self.token_setup(token, ELLIPSIS, self.start, self.cur)
                        else:
                            self.tok_backup(c)
                        self.tok_backup(46)
                    else:
                        self.tok_backup(c)
                    return self.token_setup(token, DOT, self.start, self.cur)

                # Number
                if isdigit(c):
                    return self.tok_number(token, c)

                # (f_string_quote cannot match here: it needs a letter prefix,
                # which the identifier branch above handled.)
                result = self.letter_quote(token, c)
                if result is not None:
                    return result

                # Line continuation
                if c == _BSL:
                    c = self.tok_continuation_line()
                    if c == -1:
                        return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                    self.cont_line = 1
                    continue        # goto again: read next line

                return self.tok_punctuation(current_tok, token, c)

    def type_comment(self, token, c, blankline):
        """The type comment part of the comment branch of tok_get_normal_mode:
        (token type or None when it is not a type comment, prefix end)."""
        # Spaces in the prefix are treated as "zero or more spaces or tabs".
        data = self.data
        p = self.start
        current_starting_col_offset = self.starting_col_offset
        prefix = b"# type: "
        k = 0
        while k < len(prefix) and p < self.cur:
            if prefix[k] == _SPACE:
                while _at(data, p) == _SPACE or _at(data, p) == _TAB:
                    p += 1
                    current_starting_col_offset += 1
            elif prefix[k] == _at(data, p):
                p += 1
                current_starting_col_offset += 1
            else:
                break
            k += 1
        # This is a type comment if we matched all of the prefix.
        if k < len(prefix):
            return None, p
        ignore_end = p + 6
        ignore_end_col_offset = current_starting_col_offset + 6
        self.tok_backup(c)      # don't eat the newline or EOF
        type_start = p
        # A TYPE_IGNORE is "type: ignore" followed by the end of the token or
        # anything ASCII and non-alphanumeric.
        is_type_ignore = (self.cur >= ignore_end and data[p:p + 6] == b"ignore"
                          and not (self.cur > ignore_end
                                   and (data[ignore_end] >= 128 or isalnum(data[ignore_end]))))
        if is_type_ignore:
            p_start = ignore_end
            p_end = self.cur
            # If this type ignore is the only thing on the line, consume the newline also.
            if blankline:
                self.tok_nextc()
                self.atbol = 1
            return self.type_comment_token_setup(token, TYPE_IGNORE, ignore_end_col_offset, self.col_offset,
                                                 p_start, p_end), p
        return self.type_comment_token_setup(token, TYPE_COMMENT, current_starting_col_offset, self.col_offset,
                                             type_start, self.cur), p

    def tok_number(self, token, c):
        if c == 48:
            # Hex, octal or binary -- maybe.
            c = self.tok_nextc()
            if c == 120 or c == 88:
                # Hex
                c = self.tok_nextc()
                while True:
                    if c == 95:
                        c = self.tok_nextc()
                    if not isxdigit(c):
                        self.tok_backup(c)
                        return self.token_setup(token, self.syntaxerror("invalid hexadecimal literal"), None, None)
                    c = self.tok_nextc()
                    while isxdigit(c):
                        c = self.tok_nextc()
                    if c != 95:
                        break
                if not self.verify_end_of_number(c, "hexadecimal"):
                    return self.token_setup(token, ERRORTOKEN, None, None)
            elif c == 111 or c == 79:
                # Octal
                c = self.tok_nextc()
                while True:
                    if c == 95:
                        c = self.tok_nextc()
                    if c < 48 or c >= 56:
                        if isdigit(c):
                            return self.token_setup(token, self.syntaxerror(
                                f"invalid digit '{chr(c)}' in octal literal"), None, None)
                        self.tok_backup(c)
                        return self.token_setup(token, self.syntaxerror("invalid octal literal"), None, None)
                    c = self.tok_nextc()
                    while 48 <= c < 56:
                        c = self.tok_nextc()
                    if c != 95:
                        break
                if isdigit(c):
                    return self.token_setup(token, self.syntaxerror(
                        f"invalid digit '{chr(c)}' in octal literal"), None, None)
                if not self.verify_end_of_number(c, "octal"):
                    return self.token_setup(token, ERRORTOKEN, None, None)
            elif c == 98 or c == 66:
                # Binary
                c = self.tok_nextc()
                while True:
                    if c == 95:
                        c = self.tok_nextc()
                    if c != 48 and c != 49:
                        if isdigit(c):
                            return self.token_setup(token, self.syntaxerror(
                                f"invalid digit '{chr(c)}' in binary literal"), None, None)
                        self.tok_backup(c)
                        return self.token_setup(token, self.syntaxerror("invalid binary literal"), None, None)
                    c = self.tok_nextc()
                    while c == 48 or c == 49:
                        c = self.tok_nextc()
                    if c != 95:
                        break
                if isdigit(c):
                    return self.token_setup(token, self.syntaxerror(
                        f"invalid digit '{chr(c)}' in binary literal"), None, None)
                if not self.verify_end_of_number(c, "binary"):
                    return self.token_setup(token, ERRORTOKEN, None, None)
            else:
                nonzero = 0
                # maybe old-style octal; c is first char of it
                # in any case, allow '0' as a literal
                while True:
                    if c == 95:
                        c = self.tok_nextc()
                        if not isdigit(c):
                            self.tok_backup(c)
                            return self.token_setup(token, self.syntaxerror("invalid decimal literal"), None, None)
                    if c != 48:
                        break
                    c = self.tok_nextc()
                zeros_end = self.cur
                if isdigit(c):
                    nonzero = 1
                    c = self.tok_decimal_tail()
                    if c == 0:
                        return self.token_setup(token, ERRORTOKEN, None, None)
                if c == 46:
                    c = self.tok_nextc()
                    return self.tok_fraction(token, c)
                elif c in (101, 69, 106, 74):
                    return self.tok_exponent(token, c)
                elif nonzero and not self.tok_extra_tokens:
                    # Old-style octal: now disallowed.
                    self.tok_backup(c)
                    return self.token_setup(token, self.syntaxerror_known_range(
                        self.start + 1 - self.line_start, zeros_end - self.line_start,
                        "leading zeros in decimal integer literals are not permitted; "
                        "use an 0o prefix for octal integers"), None, None)
                if not self.verify_end_of_number(c, "decimal"):
                    return self.token_setup(token, ERRORTOKEN, None, None)
        else:
            # Decimal
            c = self.tok_decimal_tail()
            if c == 0:
                return self.token_setup(token, ERRORTOKEN, None, None)
            # Accept floating-point numbers.
            if c == 46:
                c = self.tok_nextc()
                return self.tok_fraction(token, c)
            return self.tok_exponent(token, c)
        self.tok_backup(c)
        return self.token_setup(token, NUMBER, self.start, self.cur)

    def tok_fraction(self, token, c):
        """The `fraction:` label of the number branch."""
        if isdigit(c):
            c = self.tok_decimal_tail()
            if c == 0:
                return self.token_setup(token, ERRORTOKEN, None, None)
        return self.tok_exponent(token, c)

    def tok_exponent(self, token, c):
        """From the exponent test of the number branch to its end (the
        `exponent:` and `imaginary:` labels)."""
        if c == 101 or c == 69:
            e = c
            # Exponent part
            c = self.tok_nextc()
            if c == 43 or c == 45:
                c = self.tok_nextc()
                if not isdigit(c):
                    self.tok_backup(c)
                    return self.token_setup(token, self.syntaxerror("invalid decimal literal"), None, None)
            elif not isdigit(c):
                self.tok_backup(c)
                if not self.verify_end_of_number(e, "decimal"):
                    return self.token_setup(token, ERRORTOKEN, None, None)
                self.tok_backup(e)
                return self.token_setup(token, NUMBER, self.start, self.cur)
            c = self.tok_decimal_tail()
            if c == 0:
                return self.token_setup(token, ERRORTOKEN, None, None)
        if c == 106 or c == 74:
            # Imaginary part
            c = self.tok_nextc()
            if not self.verify_end_of_number(c, "imaginary"):
                return self.token_setup(token, ERRORTOKEN, None, None)
        elif not self.verify_end_of_number(c, "decimal"):
            return self.token_setup(token, ERRORTOKEN, None, None)
        self.tok_backup(c)
        return self.token_setup(token, NUMBER, self.start, self.cur)

    def f_string_quote(self, token, c):
        """The `f_string_quote:` label: c is the quote after an f/t prefix."""
        quote = c
        quote_size = 1      # 1 or 3

        # Nodes of type STRING, especially multi line strings must be handled
        # differently in order to get both the starting line number and the
        # column offset right (cf. issue 16806).
        self.first_lineno = self.lineno
        self.multi_line_start = self.line_start

        # Find the quote size and start of string
        after_quote = self.tok_nextc()
        if after_quote == quote:
            after_after_quote = self.tok_nextc()
            if after_after_quote == quote:
                quote_size = 3
            else:
                self.tok_backup(after_after_quote)
                self.tok_backup(after_quote)
        if after_quote != quote:
            self.tok_backup(after_quote)

        p_start = self.start
        p_end = self.cur
        if self.tok_mode_stack_index + 1 >= MAXFSTRINGLEVEL:
            return self.token_setup(token, self.syntaxerror("too many nested f-strings or t-strings"),
                                    p_start, p_end)
        the_current_tok = TokenizerMode(TOK_FSTRING_MODE)
        self.tok_mode_stack_index += 1
        if self.tok_mode_stack_index == len(self.tok_mode_stack):
            self.tok_mode_stack.append(the_current_tok)
        else:
            self.tok_mode_stack[self.tok_mode_stack_index] = the_current_tok
        the_current_tok.quote = quote
        the_current_tok.quote_size = quote_size
        the_current_tok.start = self.start
        the_current_tok.multi_line_start = self.line_start
        the_current_tok.first_line = self.lineno

        first = self.data[self.start]
        second = tolower(_at(self.data, self.start + 1))
        string_kind = FSTRING
        if first in (116, 84):          # t
            the_current_tok.raw = second == 114
            string_kind = TSTRING
        elif first in (102, 70):        # f
            the_current_tok.raw = second == 114
        else:                           # r
            the_current_tok.raw = 1
            if second == 116:
                string_kind = TSTRING
        the_current_tok.string_kind = string_kind
        return self.token_setup(token, TSTRING_START if string_kind == TSTRING else FSTRING_START,
                                p_start, p_end)

    def letter_quote(self, token, c):
        """The `letter_quote:` label: a string literal if c is a quote, else None."""
        if c != _SQ and c != _DQ:
            return None
        quote = c
        quote_size = 1      # 1 or 3
        end_quote_size = 0
        has_escaped_quote = 0

        # Nodes of type STRING, especially multi line strings must be handled
        # differently in order to get both the starting line number and the
        # column offset right (cf. issue 16806).
        self.first_lineno = self.lineno
        self.multi_line_start = self.line_start

        # Find the quote size and start of string
        c = self.tok_nextc()
        if c == quote:
            c = self.tok_nextc()
            if c == quote:
                quote_size = 3
            else:
                end_quote_size = 1      # empty string found
        if c != quote:
            self.tok_backup(c)

        # Get rest of string
        while end_quote_size != quote_size:
            c = self.tok_nextc()
            if self.done == E_ERROR:
                return self.token_setup(token, ERRORTOKEN, None, None)
            if self.done == E_DECODE:
                break
            if c == EOF or (quote_size == 1 and c == _NL):
                # Shift the location to the start of the string and report
                # the error from the initial quote character.
                self.cur = self.start + 1
                self.line_start = self.multi_line_start
                start = self.lineno
                self.lineno = self.first_lineno

                if self.tok_mode_stack_index > 0:
                    # In an f-string, an unterminated string whose quote
                    # matches the f-string's must be a missing '}'.
                    the_current_tok = self.tok_mode_stack[self.tok_mode_stack_index]
                    if the_current_tok.quote == quote and the_current_tok.quote_size == quote_size:
                        return self.token_setup(token, self.syntaxerror(
                            f"{self.string_prefix()}-string: expecting '}}'"), None, None)

                if quote_size == 3:
                    self.syntaxerror(f"unterminated triple-quoted string literal (detected at line {start})")
                    if c != _NL:
                        self.done = E_EOFS
                    return self.token_setup(token, ERRORTOKEN, None, None)
                if has_escaped_quote:
                    self.syntaxerror(f"unterminated string literal (detected at line {start}); "
                                     "perhaps you escaped the end quote?")
                else:
                    self.syntaxerror(f"unterminated string literal (detected at line {start})")
                if c != _NL:
                    self.done = E_EOLS
                return self.token_setup(token, ERRORTOKEN, None, None)
            if c == quote:
                end_quote_size += 1
            else:
                end_quote_size = 0
                if c == _BSL:
                    c = self.tok_nextc()        # skip escaped char
                    if c == quote:              # but record whether it was a quote
                        has_escaped_quote = 1
                    if c == _CR:
                        c = self.tok_nextc()

        return self.token_setup(token, STRING, self.start, self.cur)

    def tok_punctuation(self, current_tok, token, c):
        """The end of tok_get_normal_mode: operators and brackets."""
        inside_fstring = self.tok_mode_stack_index > 0
        is_punctuation = c == 58 or c == _RBRACE or c == 33 or c == _LBRACE
        if is_punctuation and inside_fstring and INSIDE_FSTRING_EXPR(current_tok):
            # This runs before the `{` case increments curly_bracket_depth, so
            # adjust for it to check that we are on the 0th level.
            cursor = current_tok.curly_bracket_depth - (c != _LBRACE)
            in_format_spec = current_tok.in_format_spec
            cursor_in_format_with_debug = cursor == 1 and (current_tok.in_debug or in_format_spec)
            cursor_valid = cursor == 0 or cursor_in_format_with_debug
            if cursor_valid and c != _LBRACE and self.set_ftstring_expr(token, c):
                return self.token_setup(token, ERRORTOKEN, None, None)
            if c == 58 and cursor == current_tok.curly_bracket_expr_start_depth:
                current_tok.kind = TOK_FSTRING_MODE
                current_tok.in_format_spec = 1
                return self.token_setup(token, _PyToken_OneChar(c), self.start, self.cur)

        # Check for two-character token
        c2 = self.tok_nextc()
        current_token = _PyToken_TwoChars(c, c2)
        if c == 60 and c2 == 62 and not self.barry_as_bdfl:
            current_token = OP
        if current_token != OP:
            c3 = self.tok_nextc()
            current_token3 = _PyToken_ThreeChars(c, c2, c3)
            if current_token3 != OP:
                current_token = current_token3
            else:
                self.tok_backup(c3)
            return self.token_setup(token, current_token, self.start, self.cur)
        self.tok_backup(c2)

        # Keep track of parentheses nesting level
        if c == 40 or c == 91 or c == _LBRACE:
            if self.level >= MAXLEVEL:
                return self.token_setup(token, self.syntaxerror("too many nested parentheses"), None, None)
            self.parenstack[self.level] = c
            self.parenlinenostack[self.level] = self.lineno
            self.parencolstack[self.level] = self.start - self.line_start
            self.level += 1
            if inside_fstring:
                current_tok.curly_bracket_depth += 1
        elif c == 41 or c == 93 or c == _RBRACE:
            if inside_fstring and not current_tok.curly_bracket_depth and c == _RBRACE:
                return self.token_setup(token, self.syntaxerror(
                    f"{self.string_prefix()}-string: single '}}' is not allowed"), None, None)
            if not self.tok_extra_tokens and not self.level:
                return self.token_setup(token, self.syntaxerror(f"unmatched '{chr(c)}'"), None, None)
            if self.level > 0:
                self.level -= 1
                opening = self.parenstack[self.level]
                if not ((opening == 40 and c == 41) or (opening == 91 and c == 93)
                        or (opening == _LBRACE and c == _RBRACE)):
                    # If the opening bracket belongs to an f-string's
                    # expression part (e.g. f"{)}") and the closing bracket is
                    # an arbitrary nested expression, raise an unmatched
                    # parentheses error instead of matching a different
                    # syntactical construct with it.
                    if inside_fstring and opening == _LBRACE:
                        previous_bracket = current_tok.curly_bracket_depth - 1
                        if previous_bracket == current_tok.curly_bracket_expr_start_depth:
                            return self.token_setup(token, self.syntaxerror(
                                f"{self.string_prefix()}-string: unmatched '{chr(c)}'"), None, None)
                    if self.tok_extra_tokens:
                        pass        # Allow other mismatched brackets.
                    elif self.parenlinenostack[self.level] != self.lineno:
                        return self.token_setup(token, self.syntaxerror(
                            f"closing parenthesis '{chr(c)}' does not match opening parenthesis "
                            f"'{chr(opening)}' on line {self.parenlinenostack[self.level]}"), None, None)
                    else:
                        return self.token_setup(token, self.syntaxerror(
                            f"closing parenthesis '{chr(c)}' does not match opening parenthesis "
                            f"'{chr(opening)}'"), None, None)
            if inside_fstring:
                current_tok.curly_bracket_depth -= 1
                if current_tok.curly_bracket_depth < 0:
                    return self.token_setup(token, self.syntaxerror(
                        f"{self.string_prefix()}-string: unmatched '{chr(c)}'"), None, None)
                if c == _RBRACE and current_tok.curly_bracket_depth == current_tok.curly_bracket_expr_start_depth:
                    current_tok.curly_bracket_expr_start_depth -= 1
                    current_tok.kind = TOK_FSTRING_MODE
                    current_tok.in_format_spec = 0
                    current_tok.in_debug = 0

        if not chr(c).isprintable():
            return self.token_setup(token, self.syntaxerror(f"invalid non-printable character U+{c:04X}"),
                                    None, None)

        if c == 61 and INSIDE_FSTRING_EXPR_AT_TOP(current_tok):
            current_tok.in_debug = 1

        # Punctuation character
        return self.token_setup(token, _PyToken_OneChar(c), self.start, self.cur)

    def tok_get_fstring_mode(self, current_tok, token):
        p_start = p_end = None
        end_quote_size = 0
        unicode_escape = 0

        self.start = self.cur
        self.first_lineno = self.lineno
        self.starting_col_offset = self.col_offset

        # If we start with a bracket, we defer to the normal mode as there is
        # nothing for us to tokenize before it.
        start_char = self.tok_nextc()
        if start_char == _LBRACE:
            peek1 = self.tok_nextc()
            self.tok_backup(peek1)
            if peek1 != _LBRACE:
                current_tok.last_expr_start = self.cur
            self.tok_backup(start_char)
            if peek1 != _LBRACE:
                current_tok.curly_bracket_expr_start_depth += 1
                if current_tok.curly_bracket_expr_start_depth >= MAX_EXPR_NESTING:
                    return self.token_setup(token, self.syntaxerror(
                        f"{self.string_prefix()}-string: expressions nested too deeply"), p_start, p_end)
                self.tok_mode_stack[self.tok_mode_stack_index].kind = TOK_REGULAR_MODE
                return self.tok_get_normal_mode(current_tok, token)
        else:
            self.tok_backup(start_char)

        # Check if we are at the end of the string
        at_end = True
        for _ in range(current_tok.quote_size):
            quote = self.tok_nextc()
            if quote != current_tok.quote:
                self.tok_backup(quote)
                at_end = False
                break
        if at_end:
            p_start = self.start
            p_end = self.cur
            self.tok_mode_stack_index -= 1
            return self.token_setup(token, TSTRING_END if current_tok.string_kind == TSTRING else FSTRING_END,
                                    p_start, p_end)

        # f_string_middle:
        middle = TSTRING_MIDDLE if current_tok.string_kind == TSTRING else FSTRING_MIDDLE
        self.multi_line_start = self.line_start
        while end_quote_size != current_tok.quote_size:
            c = self.tok_nextc()
            if self.done == E_ERROR or self.done == E_DECODE:
                return self.token_setup(token, ERRORTOKEN, p_start, p_end)
            in_format_spec = current_tok.in_format_spec and INSIDE_FSTRING_EXPR(current_tok)

            if c == EOF or (current_tok.quote_size == 1 and c == _NL):
                if self.decoding_erred:
                    return self.token_setup(token, ERRORTOKEN, p_start, p_end)

                # In a format spec a newline ends the format spec: return to
                # the regular mode.
                if in_format_spec and c == _NL:
                    if current_tok.quote_size == 1:
                        prefix = self.string_prefix()
                        return self.token_setup(token, self.syntaxerror(
                            f"{prefix}-string: newlines are not allowed in format specifiers for single "
                            f"quoted {prefix}-strings"), p_start, p_end)
                    self.tok_backup(c)
                    self.tok_mode_stack[self.tok_mode_stack_index].kind = TOK_REGULAR_MODE
                    current_tok.in_format_spec = 0
                    return self.token_setup(token, middle, self.start, self.cur)

                # Shift the location to the start of the string and report the
                # error from the initial quote character.
                self.cur = current_tok.start + 1
                self.line_start = current_tok.multi_line_start
                start = self.lineno
                self.lineno = self.tok_mode_stack[self.tok_mode_stack_index].first_line

                if current_tok.quote_size == 3:
                    self.syntaxerror(f"unterminated triple-quoted {self.string_prefix()}-string literal "
                                     f"(detected at line {start})")
                    if c != _NL:
                        self.done = E_EOFS
                    return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                return self.token_setup(token, self.syntaxerror(
                    f"unterminated {self.string_prefix()}-string literal (detected at line {start})"),
                    p_start, p_end)

            if c == current_tok.quote:
                end_quote_size += 1
                continue
            end_quote_size = 0

            if c == _LBRACE:
                peek = self.tok_nextc()
                if peek != _LBRACE or in_format_spec:
                    self.tok_backup(peek)
                    current_tok.last_expr_start = self.cur
                    self.tok_backup(c)
                    current_tok.curly_bracket_expr_start_depth += 1
                    if current_tok.curly_bracket_expr_start_depth >= MAX_EXPR_NESTING:
                        return self.token_setup(token, self.syntaxerror(
                            f"{self.string_prefix()}-string: expressions nested too deeply"), p_start, p_end)
                    self.tok_mode_stack[self.tok_mode_stack_index].kind = TOK_REGULAR_MODE
                    current_tok.in_format_spec = 0
                    p_start = self.start
                    p_end = self.cur
                else:
                    p_start = self.start
                    p_end = self.cur - 1
                return self.token_setup(token, middle, p_start, p_end)
            elif c == _RBRACE:
                if unicode_escape:
                    return self.token_setup(token, middle, self.start, self.cur)
                peek = self.tok_nextc()
                # The tokenizer can only be in the format spec if the
                # expression scanning is complete and we are not at the top
                # level of the bracket stack; format specifiers can't legally
                # use double brackets, so we can bypass it here.
                cursor = current_tok.curly_bracket_depth
                if peek == _RBRACE and not in_format_spec and cursor == 0:
                    p_start = self.start
                    p_end = self.cur - 1
                else:
                    self.tok_backup(peek)
                    self.tok_backup(c)
                    self.tok_mode_stack[self.tok_mode_stack_index].kind = TOK_REGULAR_MODE
                    current_tok.in_format_spec = 0
                    p_start = self.start
                    p_end = self.cur
                return self.token_setup(token, middle, p_start, p_end)
            elif c == _BSL:
                peek = self.tok_nextc()
                if peek == _CR:
                    peek = self.tok_nextc()
                # A backslash right before a curly brace: restore and let the
                # loop handle the brace on the next iteration.
                if peek == _LBRACE or peek == _RBRACE:
                    if not current_tok.raw:
                        if self.warn_invalid_escape_sequence(peek):
                            return self.token_setup(token, ERRORTOKEN, p_start, p_end)
                    self.tok_backup(peek)
                    continue
                if not current_tok.raw:
                    if peek == 78:      # Handle named unicode escapes (\N{BULLET})
                        peek = self.tok_nextc()
                        if peek == _LBRACE:
                            unicode_escape = 1
                        else:
                            self.tok_backup(peek)
                # else: skip the escaped character

        # Back up the quotes to emit a final FSTRING_MIDDLE and add the quotes
        # to the FSTRING_END in the next iteration.
        for _ in range(current_tok.quote_size):
            self.tok_backup(current_tok.quote)
        return self.token_setup(token, middle, self.start, self.cur)

    def get(self):
        """_PyTokenizer_Get."""
        token = Token()
        current_tok = self.tok_mode_stack[self.tok_mode_stack_index]
        if current_tok.kind == TOK_REGULAR_MODE:
            result = self.tok_get_normal_mode(current_tok, token)
        else:
            result = self.tok_get_fstring_mode(current_tok, token)
        if self.decoding_erred:
            result = ERRORTOKEN
            self.done = E_DECODE
        token.type = result
        start, end = token.start, token.end
        if start is None or end is None or end <= start:
            token.string = ""
        else:
            token.string = self.data[start:end].decode("utf-8", "replace")
        return token

    # ---- underflow (Parser/tokenizer/{string,utf8,readline}_tokenizer.c)

    def tok_underflow_string(self):
        data = self.data
        end = data.find(b"\n", self.inp)
        if end >= 0:
            end += 1
        else:
            end = len(data)
            if end == self.inp:
                self.done = E_EOF
                return 0
        if self.start is None:
            self.buf = self.cur
        self.line_start = self.cur
        self.lineno += 1
        self.col_offset = 0
        self.inp = end
        return 1

    def tok_readline_string(self):
        try:
            raw_line = self.readline()
        except StopIteration:
            return 1
        except Exception as error:
            self.error = error
            self.error_ret()
            return 0
        if self.encoding is not None:
            if not isinstance(raw_line, bytes):
                self.error = TypeError("readline() returned a non-bytes object")
                self.error_ret()
                return 0
            try:
                line = raw_line.decode(self.encoding, "replace")
            except Exception as error:
                self.error = error
                self.error_ret()
                return 0
        else:
            if not isinstance(raw_line, str):
                self.error = TypeError("readline() returned a non-string object")
                self.error_ret()
                return 0
            line = raw_line
        try:
            buf = line.encode("utf-8")
        except UnicodeEncodeError as error:
            self.error = error
            self.error_ret()
            return 0
        self.data += buf
        self.inp += len(buf)
        self.line_start = self.cur
        return 1

    def tok_underflow_readline(self):
        if self.start is None and self.tok_mode_stack_index == 0:
            self.data = bytearray()
            self.cur = self.inp = self.buf
        if not self.tok_readline_string():
            return 0
        if self.inp == self.cur:
            self.done = E_EOF
            return 0
        self.implicit_newline = 0
        if self.data[self.inp - 1] != _NL:
            # Last line does not end in \n, fake one
            self.data.append(_NL)
            self.inp += 1
            self.implicit_newline = 1
        self.lineno += 1
        self.col_offset = 0
        # The default encoding is UTF-8, so make sure we don't have any
        # non-UTF-8 sequences in it.  (Lines arrive as str or decoded with
        # "replace", so they always are.)
        return 1


# ---- helpers.c

def byte_col_to_char_col(data, line_start, byte_col):
    """Convert a 1-based column in bytes into a 1-based column in characters."""
    char_col = 1
    for i in range(line_start, line_start + byte_col - 1):
        if (_at(data, i) & 0xC0) != 0x80:
            char_col += 1
    return char_col


def scan_identifier(s):
    """_PyUnicode_ScanIdentifier: the length of the identifier prefix of s."""
    if s.isidentifier():
        return len(s)
    if not s[0].isidentifier():
        return 0
    i = 1
    while i < len(s) and ("a" + s[i]).isidentifier():
        i += 1
    return i


def translate_newlines(s, exec_input, preserve_crlf):
    s = s.split(b"\0", 1)[0]
    if not preserve_crlf:
        s = s.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    # If this is exec input, add a newline to the end of the string if there
    # isn't one already.
    if exec_input and s and not s.endswith(b"\n"):
        s += b"\n"
    return s


def get_normal_name(s):
    """The canonical name of utf-8 and latin-1 spellings, else s."""
    buf = s[:12].replace("_", "-").lower()
    if buf == "utf-8" or buf.startswith("utf-8-"):
        return "utf-8"
    if (buf in ("latin-1", "iso-8859-1", "iso-latin-1")
            or buf.startswith(("latin-1-", "iso-8859-1-", "iso-latin-1-"))):
        return "iso-8859-1"
    return s


def get_coding_spec(data, offset, size):
    """The coding spec in the line at data[offset:offset + size], or None."""
    # The coding spec must be in a comment, and that comment must be the only
    # statement on the source code line.
    i = 0
    while i < size - 6:
        ch = data[offset + i]
        if ch == _HASH:
            break
        if ch != _SPACE and ch != _TAB and ch != _FF:
            return None
        i += 1
    while i < size - 6:
        t = offset + i
        if data[t:t + 6] == b"coding":
            t += 6
            if _at(data, t) != 58 and _at(data, t) != 61:      # ':' '='
                i += 1
                continue
            t += 1
            while _at(data, t) == _SPACE or _at(data, t) == _TAB:
                t += 1
            begin = t
            while True:
                ch = _at(data, t)
                if not (isalnum(ch) or ch == 45 or ch == 95 or ch == 46):
                    break
                t += 1
            if begin < t:
                return get_normal_name(data[begin:t].decode("ascii"))
        i += 1
    return None


def check_coding_spec(tok, data, offset, size):
    """_PyTokenizer_check_coding_spec with the string tokenizer's
    set_readline (which records the encoding in tok.enc)."""
    if tok.cont_line:
        # It's a continuation line, so it can't be a coding spec.
        tok.decoding_state = STATE_NORMAL
        return 1
    cs = get_coding_spec(data, offset, size)
    if cs is None:
        for i in range(offset, offset + size):
            ch = data[i]
            if ch == _HASH or ch == _NL or ch == _CR:
                break
            if ch != _SPACE and ch != _TAB and ch != _FF:
                # Stop checking coding spec after a line containing anything
                # except a comment.
                tok.decoding_state = STATE_NORMAL
                break
        return 1
    tok.decoding_state = STATE_NORMAL
    if tok.encoding is None:
        if cs != "utf-8":
            tok.enc = cs
        tok.encoding = cs
    elif tok.encoding != cs:     # compare cs with BOM
        tok.line_start = tok.cur = offset
        tok.syntaxerror_known_range(0, size, f"encoding problem: {cs} with BOM")
        error = tok.error
        tok.error_ret()
        raise error
    return 1


def ensure_utf8(tok, data, offset, lineno):
    try:
        data[offset:].decode("utf-8")
        return
    except UnicodeDecodeError as error:
        badchar = offset + error.start
    newlines = data.count(b"\n", offset, badchar)
    line_start = data.rfind(b"\n", offset, badchar) + 1 if newlines else offset
    lineno += newlines
    tok.lineno = lineno
    tok.line_start = line_start
    tok.cur = badchar
    tok.syntaxerror_known_range(
        badchar - line_start + 1, badchar - line_start + 1,
        f"Non-UTF-8 code starting with '\\x{data[badchar]:02x}'"
        f"{' in file ' if tok.filename is not None else ''}{tok.filename or ''} on line {lineno}, "
        "but no encoding declared; see https://peps.python.org/pep-0263/ for details")
    error = tok.error
    tok.error_ret()
    raise error


def decode_str(tok, source, single, preserve_crlf):
    """Decode source for use as the buffer of tok: honour a BOM and a coding
    spec on the first two lines."""
    s = translate_newlines(source, single, preserve_crlf)
    tok.input = s
    tok.enc = None
    tok.decoding_state = STATE_SEEK_CODING
    if s.startswith(b"\xef\xbb\xbf"):
        tok.encoding = "utf-8"
        s = s[3:]
    tok.data = s
    newl0 = s.find(b"\n")
    newl1 = s.find(b"\n", newl0 + 1) if newl0 >= 0 else -1
    tok.enc = None
    # Lines 1 and 2 are checked separately since check_coding_spec assumes a
    # single line as input.
    if newl0 >= 0:
        tok.lineno = 1
        check_coding_spec(tok, s, 0, newl0)
        if tok.enc is None and tok.decoding_state != STATE_NORMAL and newl1 >= 0:
            tok.lineno = 2
            check_coding_spec(tok, s, newl0 + 1, newl1 - newl0)
    tok.lineno = 0
    if tok.enc is not None:
        try:
            s = s.decode(tok.enc).encode("utf-8")
        except Exception:
            tok.error_ret()
            raise
        s = translate_newlines(s, single, preserve_crlf)
        tok.input = s
    else:
        ensure_utf8(tok, s, 0, 1)
    tok.str = s
    return s


def from_string(source, exec_input, preserve_crlf=False):
    """_PyTokenizer_FromString: tokenize bytes, honouring a coding spec."""
    tok = Tokenizer()
    decoded = decode_str(tok, bytes(source), exec_input, preserve_crlf)
    tok.data = decoded
    tok.buf = tok.cur = tok.inp = 0
    tok.underflow = Tokenizer.tok_underflow_string  # a plain function, as in C, so tok stays acyclic
    return tok


def from_utf8(source, exec_input, preserve_crlf=False):
    """_PyTokenizer_FromUTF8: tokenize UTF-8 bytes, ignoring coding specs."""
    tok = Tokenizer()
    translated = translate_newlines(bytes(source), exec_input, preserve_crlf)
    tok.input = translated
    tok.decoding_state = STATE_NORMAL
    tok.enc = None
    tok.str = translated
    tok.encoding = "utf-8"
    tok.data = translated
    tok.buf = tok.cur = tok.inp = 0
    tok.underflow = Tokenizer.tok_underflow_string  # a plain function, as in C, so tok stays acyclic
    return tok


def from_readline(readline, enc, exec_input, preserve_crlf):
    """_PyTokenizer_FromReadline: readline returns str, or bytes in enc."""
    tok = Tokenizer()
    tok.data = bytearray()
    tok.encoding = enc
    tok.decoding_state = STATE_NORMAL
    tok.underflow = Tokenizer.tok_underflow_readline  # a plain function, as in C, so tok stays acyclic
    tok.readline = readline
    return tok


def raise_init_error(error, filename):
    """_PyTokenizer_raise_init_error: the exception to raise for an error
    from from_string/from_utf8."""
    if not isinstance(error, (LookupError, SyntaxError, ValueError, UnicodeDecodeError)):
        return error
    if isinstance(error, SyntaxError):
        error.filename = filename
        return error
    return SyntaxError(str(error), (filename, 0, -1, None))


# ---- Python/Python-tokenize.c

def byte_offset_to_character_offset_line(line, col_offset, end_col_offset):
    """_PyPegen_byte_offset_to_character_offset_line on line's UTF-8 bytes."""
    length = 0
    while col_offset < end_col_offset:
        ch = _at(line, col_offset)
        if ch < 0x80:
            col_offset += 1
        elif (ch & 0xE0) == 0xC0:
            col_offset += 2
        elif (ch & 0xF0) == 0xE0:
            col_offset += 3
        elif (ch & 0xF8) == 0xF0:
            col_offset += 4
        else:
            raise ValueError("Invalid UTF-8 sequence")
        length += 1
    return length


def byte_offset_to_character_offset_raw(data, start, col_offset):
    """_PyPegen_byte_offset_to_character_offset_raw for the NUL-terminated
    string at data[start:]."""
    nul = data.find(0, start)
    length = (nul if nul >= 0 else len(data)) - start
    if col_offset > length + 1:
        col_offset = length + 1
    # Reaching past the end decodes the terminating NUL as one more character.
    terminator = 1 if col_offset > length else 0
    return len(data[start:start + col_offset - terminator].decode("utf-8", "replace")) + terminator


class TokenizerIter:
    """_tokenize.TokenizerIter."""

    def __init__(self, readline, *, extra_tokens, encoding=None):
        self.tok = from_readline(readline, encoding, 1, 1)
        self.tok.filename = "<string>"
        if extra_tokens:
            self.tok.tok_extra_tokens = 1
        self.done = 0
        self.last_line = None
        self.last_line_bytes = b""
        self.last_lineno = 0
        self.last_end_lineno = 0
        self.byte_col_offset_diff = 0

    def __iter__(self):
        return self

    def _tokenizer_error(self):
        tok = self.tok
        errtype = SyntaxError
        done = tok.done
        if done == E_TOKEN:
            msg = "invalid token"
        elif done == E_EOF:
            error = SyntaxError("unexpected EOF in multi-line statement")
            offset = tok.inp - tok.buf
            error.lineno = tok.lineno
            error.offset = offset if offset >= 0 else 0
            error.end_lineno = tok.lineno
            error.end_offset = None
            error.filename = tok.filename
            return error
        elif done == E_DEDENT:
            msg = "unindent does not match any outer indentation level"
            errtype = IndentationError
        elif done == E_INTR:
            return KeyboardInterrupt()
        elif done == E_NOMEM:
            return MemoryError()
        elif done == E_TABSPACE:
            errtype = TabError
            msg = "inconsistent use of tabs and spaces in indentation"
        elif done == E_TOODEEP:
            errtype = IndentationError
            msg = "too many levels of indentation"
        elif done == E_LINECONT:
            msg = "unexpected character after line continuation character"
        else:
            msg = "unknown tokenization error"
        size = tok.inp - tok.buf - 1      # without the newline at the end of the line
        error_line = tok.data[tok.buf:tok.buf + size].decode("utf-8", "replace")
        line_bytes = error_line.encode("utf-8", "surrogatepass")
        offset = byte_offset_to_character_offset_raw(line_bytes, 0, tok.inp - tok.buf)
        return errtype(msg, (tok.filename, tok.lineno, offset, error_line, None, None))

    def __next__(self):
        tok = self.tok
        token = tok.get()
        type = token.type
        if type == ERRORTOKEN:
            error = tok.error if tok.error is not None else self._tokenizer_error()
            tok.error = None
            raise error
        if self.done:
            raise StopIteration("EOF")
        string = token.string

        is_trailing_token = type == ENDMARKER or (type == DEDENT and tok.done == E_EOF)
        string_lit = type in ISSTRINGLIT
        line_start = tok.multi_line_start if string_lit else tok.line_start
        line_changed = 1
        if tok.tok_extra_tokens and is_trailing_token:
            line = ""
            line_bytes = b""
        else:
            size = tok.inp - line_start
            if size >= 1 and tok.implicit_newline:
                size -= 1
            # _get_current_line: the line is cached while the line number stays.
            if tok.lineno != self.last_lineno:
                line_bytes = bytes(tok.data[line_start:line_start + size])
                line = line_bytes.decode("utf-8", "replace")
                self.last_line = line
                self.last_line_bytes = line_bytes
                self.byte_col_offset_diff = 0
            else:
                line = self.last_line
                line_bytes = self.last_line_bytes
                line_changed = 0
                if line is None:
                    # No line was cached yet (empty input): the C returns NULL
                    # without an exception, which ends the iteration.
                    if type == ENDMARKER:
                        self.done = 1
                    raise StopIteration

        lineno = tok.first_lineno if string_lit else tok.lineno
        end_lineno = tok.lineno
        col_offset = -1
        end_col_offset = -1
        # _get_col_offsets
        byte_offset = -1
        if token.start is not None and token.start >= line_start:
            byte_offset = token.start - line_start
            if line_changed:
                col_offset = byte_offset_to_character_offset_line(line_bytes, 0, byte_offset)
                self.byte_col_offset_diff = byte_offset - col_offset
            else:
                col_offset = byte_offset - self.byte_col_offset_diff
        if token.end is not None and token.end >= tok.line_start:
            end_byte_offset = token.end - tok.line_start
            if lineno == end_lineno:
                # The whole token is on one line, so count from the token
                # start: using the whole line is slow for very long lines.
                token_col_offset = byte_offset_to_character_offset_line(line_bytes, byte_offset, end_byte_offset)
                end_col_offset = col_offset + token_col_offset
                self.byte_col_offset_diff += token.end - token.start - token_col_offset
            else:
                end_col_offset = byte_offset_to_character_offset_raw(tok.data, tok.line_start, end_byte_offset)
                self.byte_col_offset_diff += end_byte_offset - end_col_offset
        self.last_lineno = lineno
        self.last_end_lineno = end_lineno

        if tok.tok_extra_tokens:
            if is_trailing_token:
                lineno = end_lineno = lineno + 1
                col_offset = end_col_offset = 0
            # Adjustments to match the original Python tokenize implementation
            if DEDENT < type < OP:
                type = OP
            elif type == NEWLINE:
                if not tok.implicit_newline:
                    string = "\r\n" if tok.data[tok.start] == _CR else "\n"
                end_col_offset += 1
            elif type == NL:
                if tok.implicit_newline:
                    string = ""

        if type == ENDMARKER:
            self.done = 1
        return (type, string, (lineno, col_offset), (end_lineno, end_col_offset), line)
