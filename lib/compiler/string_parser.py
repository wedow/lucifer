"""String literal decoding: Parser/string_parser.c, with the escape decoders it
calls (Objects/unicodeobject.c _PyUnicode_DecodeUnicodeEscapeInternal2,
Objects/bytesobject.c _PyBytes_DecodeEscape2).  f-strings and t-strings are
assembled by the grammar's actions (helpers.py)."""
import re
import warnings

from token import FSTRING_MIDDLE, FSTRING_END, TSTRING_MIDDLE, TSTRING_END

_SIMPLE_ESCAPES = {
    ord("\\"): "\\", ord("'"): "'", ord('"'): '"', ord("b"): "\b", ord("f"): "\014",
    ord("t"): "\t", ord("n"): "\n", ord("r"): "\r", ord("v"): "\013", ord("a"): "\007",
}
_OCTAL = b"01234567"
_HEX = b"0123456789abcdefABCDEF"
_BACKSLASH = re.compile(rb"\\")


def warn_invalid_escape_sequence(p, buffer, first_invalid_escape, t):
    """buffer is the bytes the escape decoder saw; first_invalid_escape an
    offset into it."""
    if p.call_invalid_rules:
        # Do not report warnings if we are in the second pass of the parser
        # to avoid showing the warning twice.
        return
    c = buffer[first_invalid_escape]
    if (t.type in (FSTRING_MIDDLE, FSTRING_END, TSTRING_MIDDLE, TSTRING_END)
            and c in (ord("{"), ord("}"))):
        # in this case the tokenizer has already emitted a warning,
        # see Parser/tokenizer/helpers.c:warn_invalid_escape_sequence
        return

    octal = ord("4") <= c <= ord("7")
    escape = buffer[first_invalid_escape:first_invalid_escape + 3].decode("latin-1")
    if octal:
        msg = (f'"\\{escape}" is an invalid octal escape sequence. '
               "Such sequences will not work in the future. "
               f'Did you mean "\\\\{escape}"? A raw string is also an option.')
    else:
        msg = (f'"\\{chr(c)}" is an invalid escape sequence. '
               "Such sequences will not work in the future. "
               f'Did you mean "\\\\{chr(c)}"? A raw string is also an option.')
    category = SyntaxWarning if p.feature_version >= 12 else DeprecationWarning

    # Calculate the lineno and the col_offset of the invalid escape sequence
    lineno = t.lineno
    col_offset = t.col_offset
    for ch in buffer[:first_invalid_escape]:
        if ch == ord("\n"):
            lineno += 1
            col_offset = 0
        else:
            col_offset += 1

    # Count the number of quotes in the token
    if lineno == t.lineno:
        quote_count = 0
        first_quote = None
        for ch in t.string:
            if ch in "'\"":
                if quote_count == 0:
                    first_quote = ch
                if ch == first_quote:
                    quote_count += 1
            else:
                break
        col_offset += quote_count

    try:
        warnings.warn_explicit(msg, category, p.tok.filename, lineno)
    except category:
        # Replace the Syntax/DeprecationWarning exception with a SyntaxError
        # to get a more accurate error report.  The SyntaxError points to the
        # token t, since raise_error uses p.tokens[p.fill - 1] for the error
        # location if p.known_err_token is not set.
        from .pegen import RAISE_ERROR_KNOWN_LOCATION
        p.known_err_token = t
        if octal:
            RAISE_ERROR_KNOWN_LOCATION(
                p, SyntaxError, lineno, col_offset - 1, lineno, col_offset + 1,
                '"\\%s" is an invalid octal escape sequence. '
                'Did you mean "\\\\%s"? A raw string is also an option.', escape, escape)
        else:
            RAISE_ERROR_KNOWN_LOCATION(
                p, SyntaxError, lineno, col_offset - 1, lineno, col_offset + 1,
                '"\\%c" is an invalid escape sequence. '
                'Did you mean "\\\\%c"? A raw string is also an option.', c, c)


def decode_unicode_escape(s):
    """_PyUnicode_DecodeUnicodeEscapeInternal2 on bytes s: (str, offset of the
    first invalid escape or None)."""
    out = []
    first_invalid = None
    end = len(s)
    i = 0
    while i < end:
        j = s.find(b"\\", i)
        if j < 0:
            out.append(s[i:].decode("latin-1"))
            break
        out.append(s[i:j].decode("latin-1"))
        startinpos = j
        i = j + 1
        if i >= end:
            raise UnicodeDecodeError("unicodeescape", bytes(s), startinpos, end, "\\ at end of string")
        c = s[i]
        i += 1
        if c == ord("\n"):
            continue
        simple = _SIMPLE_ESCAPES.get(c)
        if simple is not None:
            out.append(simple)
            continue
        if c in _OCTAL:
            ch = c - ord("0")
            if i < end and s[i] in _OCTAL:
                ch = (ch << 3) + s[i] - ord("0")
                i += 1
                if i < end and s[i] in _OCTAL:
                    ch = (ch << 3) + s[i] - ord("0")
                    i += 1
            if ch > 0o377 and first_invalid is None:
                first_invalid = i - 3
            out.append(chr(ch))
            continue
        if c in b"xuU":
            count, message = {ord("x"): (2, "truncated \\xXX escape"),
                              ord("u"): (4, "truncated \\uXXXX escape"),
                              ord("U"): (8, "truncated \\UXXXXXXXX escape")}[c]
            ch = 0
            while count:
                if i >= end or s[i] not in _HEX:
                    raise UnicodeDecodeError("unicodeescape", bytes(s), startinpos, i, message)
                ch = (ch << 4) + int(chr(s[i]), 16)
                i += 1
                count -= 1
            if ch > 0x10FFFF:
                raise UnicodeDecodeError("unicodeescape", bytes(s), startinpos, i, "illegal Unicode character")
            out.append(chr(ch))
            continue
        if c == ord("N"):
            message = "malformed \\N character escape"
            if i < end and s[i] == ord("{"):
                start = i + 1
                close = s.find(b"}", start)
                if close < 0:
                    raise UnicodeDecodeError("unicodeescape", bytes(s), startinpos, end, message)
                i = close
                if close > start:
                    i = close + 1
                    import unicodedata
                    try:
                        name = s[start:close].decode("latin-1")
                        ch = unicodedata.lookup(name)
                    except KeyError:
                        ch = ""
                    if len(ch) == 1:
                        out.append(ch)
                        continue
                    message = "unknown Unicode character name"
            raise UnicodeDecodeError("unicodeescape", bytes(s), startinpos, i if i <= end else end, message)
        if first_invalid is None:
            first_invalid = i - 1
        out.append("\\")
        out.append(chr(c))
    return "".join(out), first_invalid


def decode_bytes_escape(s):
    """_PyBytes_DecodeEscape2 on bytes s: (bytes, offset of the first invalid
    escape or None)."""
    out = bytearray()
    first_invalid = None
    end = len(s)
    i = 0
    while i < end:
        j = s.find(b"\\", i)
        if j < 0:
            out += s[i:]
            break
        out += s[i:j]
        i = j + 1
        if i == end:
            raise ValueError("Trailing \\ in string")
        c = s[i]
        i += 1
        if c == ord("\n"):
            continue
        simple = _SIMPLE_ESCAPES.get(c)
        if simple is not None:
            out.append(ord(simple))
            continue
        if c in _OCTAL:
            ch = c - ord("0")
            if i < end and s[i] in _OCTAL:
                ch = (ch << 3) + s[i] - ord("0")
                i += 1
                if i < end and s[i] in _OCTAL:
                    ch = (ch << 3) + s[i] - ord("0")
                    i += 1
            if ch > 0o377 and first_invalid is None:
                first_invalid = i - 3
            out.append(ch & 0xFF)
            continue
        if c == ord("x"):
            if i + 1 < end and s[i] in _HEX and s[i + 1] in _HEX:
                out.append(int(s[i:i + 2], 16))
                i += 2
                continue
            # invalid hexadecimal digits
            raise ValueError("invalid \\x escape at position %d" % (i - 2))
        if first_invalid is None:
            first_invalid = i - 1
        out.append(ord("\\"))
        i -= 1
    return bytes(out), first_invalid


def decode_unicode_with_escapes(parser, s, t):
    """decode_unicode_with_escapes: s is the literal's text (str).

    Like the C, non-ASCII characters are first turned into \\U escapes, so
    the offsets the warning reports count those escapes."""
    if s.isascii():
        buf = s.encode("ascii")
    else:
        parts = []
        i, n = 0, len(s)
        while i < n:
            ch = s[i]
            if ch == "\\":
                parts.append("\\")
                i += 1
                if i >= n or not s[i].isascii():
                    parts.append("u005c")
                    if i >= n:
                        break
                ch = s[i]
            if ch.isascii():
                parts.append(ch)
            else:
                parts.append("\\U%08x" % ord(ch))
            i += 1
        buf = "".join(parts).encode("ascii", "surrogateescape")
    v, first_invalid = decode_unicode_escape(buf)
    if first_invalid is not None and t is not None:
        warn_invalid_escape_sequence(parser, buf, first_invalid, t)
    return v


def decode_bytes_with_escapes(p, s, t):
    """decode_bytes_with_escapes: s is the literal's text (ASCII bytes)."""
    result, first_invalid = decode_bytes_escape(s)
    if first_invalid is not None:
        warn_invalid_escape_sequence(p, s, first_invalid, t)
    return result


def decode_string(p, raw, s, t):
    """_PyPegen_decode_string"""
    if raw:
        return s
    return decode_unicode_with_escapes(p, s, t)


def parse_string(p, t):
    """_PyPegen_parse_string: the value of the STRING token t, whose text
    includes the quotes, the r/b/u prefixes and the escapes."""
    s = t.string
    i = 0
    bytesmode = rawmode = False
    while s[i].isalpha():
        q = s[i]
        if q in "bB":
            bytesmode = True
        elif q in "rR":
            rawmode = True
        elif q not in "uU":
            break
        i += 1
    quote = s[i]
    if quote not in "'\"":
        raise SystemError("bad argument to internal function")
    # Skip the leading and trailing quote chars, then the other two of a
    # triple quote.
    body = s[i + 1:-1]
    if len(body) >= 4 and body[0] == quote and body[1] == quote:
        body = body[2:-2]

    # Avoid invoking escape decoding routines if possible.
    rawmode = rawmode or "\\" not in body
    if bytesmode:
        # Disallow non-ASCII characters.
        if not body.isascii():
            from .pegen import RAISE_SYNTAX_ERROR_KNOWN_LOCATION
            RAISE_SYNTAX_ERROR_KNOWN_LOCATION(p, t, "bytes can only contain ASCII literal characters")
        if rawmode:
            return body.encode("ascii")
        return decode_bytes_with_escapes(p, body.encode("ascii"), t)
    return decode_string(p, rawmode, body, t)
