"""Formatting: PyObject_Str, PyObject_Repr, PyObject_ASCII and PyObject_Format,
the format-spec mini-language (Python/formatter_unicode.c) for str and int,
str.format (Objects/stringlib/unicode_format.h) and %-formatting
(Objects/unicodeobject.c, PyUnicode_Format). Strings are taken apart with the
str primitives; this module makes no str method calls, as str is built on it."""

from _lucifer import (
    MISSING, error, str_join, kind, ns_get, type_lookup, type_tp_name, static_type, int_format,
    int_as_double, str_len, str_ord, str_slice, str_item, str_repeat, str_decimal, str_case,
    str_ascii, str_chr, str_find, list_append, text_repr, box_get, box_set,
)
from _lucifer import _boot, operators, object as _object

call_special = _boot.call_special
index = operators.index
getattr_hook = _object.getattr_hook

__all__ = ()

_SSIZE_MAX = 9223372036854775807


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def _sub(s, start, end):
    return str_slice(s, start, end)


def _upper(s):
    return str_case(s, 1)


# PyObject_Str and friends

def to_str(x):
    if type(x) is str:
        return x
    r = call_special(type_lookup(type(x), '__str__'), x)
    if not kind(r, 'str'):
        error(TypeError, _cat('__str__ returned non-string (type ', _name(r), ')'))
    return r


def to_repr(x):
    r = call_special(type_lookup(type(x), '__repr__'), x)
    if not kind(r, 'str'):
        error(TypeError, _cat('__repr__ returned non-string (type ', _name(r), ')'))
    return r


def to_ascii(x):
    return str_ascii(to_repr(x))


def format_value(value, spec):
    """PyObject_Format(value, spec)."""
    if str_len(spec) == 0:
        if type(value) is str:
            return value
        if type(value) is int:
            return to_str(value)
    f = type_lookup(type(value), '__format__')
    if f is MISSING:
        error(TypeError, _cat("Type ", _name(value), " doesn't define __format__"))
    r = call_special(f, value, spec)
    if not kind(r, 'str'):
        error(TypeError, _cat('__format__ must return a str, not ', _name(r)))
    return r


def object_format(self, spec):
    """object.__format__."""
    if str_len(spec) != 0:
        error(TypeError, _cat('unsupported format string passed to ', _name(self), '.__format__'))
    return to_str(self)


# The format-spec mini-language

def _unknown_type(ty, obj):
    c = str_ord(ty, 0)
    if c > 32 and c < 128:
        error(ValueError, _cat("Unknown format code '", ty, "' for object of type '", _name(obj), "'"))
    error(ValueError, _cat("Unknown format code '\\x", int_format(c, 16, False), "' for object of type '",
                           _name(obj), "'"))


def _bad_thousands(sep, ty):
    c = str_ord(ty, 0) if str_len(ty) else 0
    if c > 32 and c < 128:
        error(ValueError, _cat("Cannot specify '", sep, "' with '", ty, "'."))
    error(ValueError, _cat("Cannot specify '", sep, "' with '\\x", int_format(c, 16, False), "'."))


def _get_integer(s, pos, end):
    """Decimal digits at s[pos:end]: (value, new pos, digit count)."""
    acc = 0
    n = 0
    while pos < end:
        d = str_decimal(s, pos)
        if d < 0:
            break
        if acc > (_SSIZE_MAX - d) // 10:
            error(ValueError, 'Too many decimal digits in format string')
        acc = acc * 10 + d
        pos += 1
        n += 1
    return acc, pos, n


def _at(s, pos, end):
    return str_item(s, pos) if pos < end else ''


def parse_spec(obj, spec, default_type, default_align):
    """parse_internal_render_format_spec: (fill, align, alternate, no_neg_0,
    sign, width, thousands, precision, frac_thousands, type). Thousands are
    '', ',', '_', or '4' for an underscore every four digits."""
    pos = 0
    end = str_len(spec)
    fill = ' '
    align = default_align
    alternate = False
    no_neg_0 = False
    sign = ''
    width = -1
    thousands = ''
    precision = -1
    frac = ''
    ty = default_type
    align_specified = False
    fill_specified = False
    if end - pos >= 2 and _at(spec, pos + 1, end) in '<>=^':
        align = str_item(spec, pos + 1)
        fill = str_item(spec, pos)
        fill_specified = True
        align_specified = True
        pos += 2
    elif end - pos >= 1 and _at(spec, pos, end) in '<>=^':
        align = str_item(spec, pos)
        align_specified = True
        pos += 1
    if end - pos >= 1 and _at(spec, pos, end) in ' +-':
        sign = str_item(spec, pos)
        pos += 1
    if _at(spec, pos, end) == 'z':
        no_neg_0 = True
        pos += 1
    if _at(spec, pos, end) == '#':
        alternate = True
        pos += 1
    if not fill_specified and _at(spec, pos, end) == '0':
        fill = '0'
        if not align_specified and default_align == '>':
            align = '='
        pos += 1
    value, pos, consumed = _get_integer(spec, pos, end)
    if consumed != 0:
        width = value
    if _at(spec, pos, end) == ',':
        thousands = ','
        pos += 1
    if _at(spec, pos, end) == '_':
        if thousands != '':
            error(ValueError, "Cannot specify both ',' and '_'.")
        thousands = '_'
        pos += 1
    if _at(spec, pos, end) == ',' and thousands == '_':
        error(ValueError, "Cannot specify both ',' and '_'.")
    if _at(spec, pos, end) == '.':
        pos += 1
        precision, pos, consumed = _get_integer(spec, pos, end)
        if _at(spec, pos, end) == ',':
            if consumed == 0:
                precision = -1
            frac = ','
            pos += 1
            consumed += 1
        if _at(spec, pos, end) == '_':
            if frac != '':
                error(ValueError, "Cannot specify both ',' and '_'.")
            if consumed == 0:
                precision = -1
            frac = '_'
            pos += 1
            consumed += 1
        if _at(spec, pos, end) == ',' and frac == '_':
            error(ValueError, "Cannot specify both ',' and '_'.")
        if consumed == 0:
            error(ValueError, 'Format specifier missing precision')
    if end - pos > 1:
        error(ValueError, _cat("Invalid format specifier '", spec, "' for object of type '",
                               _name(obj), "'"))
    if end - pos == 1:
        ty = str_item(spec, pos)
    if thousands != '':
        if ty in 'defgEG%F' or ty == '':
            pass
        elif ty in 'boxX' and thousands == '_':
            thousands = '4'
        else:
            _bad_thousands(thousands, ty)
    if ty == 'n' and frac != '':
        _bad_thousands(frac, ty)
    return fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty


def _pad(fill, n):
    return str_repeat(fill, n) if n > 0 else ''


def format_str(value, spec):
    """str.__format__."""
    if str_len(spec) == 0:
        return to_str(value)
    fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty = \
        parse_spec(value, spec, 's', '<')
    if ty != 's':
        _unknown_type(ty, value)
    if sign != '':
        if sign == ' ':
            error(ValueError, 'Space not allowed in string format specifier')
        error(ValueError, 'Sign not allowed in string format specifier')
    if no_neg_0:
        error(ValueError, 'Negative zero coercion (z) not allowed in string format specifier')
    if alternate:
        error(ValueError, 'Alternate form (#) not allowed in string format specifier')
    if align == '=':
        error(ValueError, "'=' alignment not allowed in string format specifier")
    n = str_len(value)
    if (width == -1 or width <= n) and (precision == -1 or precision >= n):
        return value if type(value) is str else _sub(value, 0, n)
    if precision >= 0 and n >= precision:
        n = precision
    total = width if width > n else n
    lpad = 0
    if align == '>':
        lpad = total - n
    elif align == '^':
        lpad = (total - n) // 2
    return _cat(_pad(fill, lpad), _sub(value, 0, n), _pad(fill, total - n - lpad))


def _group(digits, min_width, sep, size):
    """_PyUnicode_InsertThousandsGrouping: digits, zero-extended to min_width
    when there is room, with sep between groups of `size` from the right."""
    remaining = str_len(digits)
    end = remaining
    if sep == '':
        n = remaining if remaining > min_width else min_width
        if n < 1:
            n = 1
        return _cat(_pad('0', n - remaining), digits)
    sep_len = str_len(sep)
    out = ''
    first = True
    while True:
        l = remaining if remaining > min_width else min_width
        if l < 1:
            l = 1
        if size < l:
            l = size
        zeros = l - remaining if l > remaining else 0
        chars = remaining if remaining < l else l
        if chars < 0:
            chars = 0
        chunk = _cat(_pad('0', zeros), _sub(digits, end - chars, end))
        out = chunk if first else _cat(chunk, sep, out)
        first = False
        end -= chars
        remaining -= chars
        min_width -= l
        if remaining <= 0 and min_width <= 0:
            return out
        min_width -= sep_len


def number_field(sign_char, prefix, digits, remainder, spec, upper):
    """calc_number_widths and fill_number for [sign][prefix]digits[remainder]
    under the parsed spec."""
    fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty = spec
    n_sign = 0
    sign_out = ''
    if sign == '+':
        n_sign = 1
        sign_out = '-' if sign_char == '-' else '+'
    elif sign == ' ':
        n_sign = 1
        sign_out = '-' if sign_char == '-' else ' '
    elif sign_char == '-':
        n_sign = 1
        sign_out = '-'
    n_non_digit = n_sign + str_len(prefix) + str_len(remainder)
    min_width = 0
    if fill == '0' and align == '=':
        min_width = width - n_non_digit
    if str_len(digits) == 0:
        grouped = ''
    else:
        gsep = '' if ty == 'n' else (',' if thousands == ',' else ('_' if thousands != '' else ''))
        grouped = _group(digits, min_width, gsep, 4 if thousands == '4' else 3)
    if upper:
        prefix = _upper(prefix)
        grouped = _upper(grouped)
    n_padding = width - (n_non_digit + str_len(grouped))
    lpad = 0
    spad = 0
    rpad = 0
    if n_padding > 0:
        if align == '<':
            rpad = n_padding
        elif align == '^':
            lpad = n_padding // 2
            rpad = n_padding - lpad
        elif align == '=':
            spad = n_padding
        else:
            lpad = n_padding
    return _cat(_pad(fill, lpad), sign_out, prefix, _pad(fill, spad), grouped, remainder,
                _pad(fill, rpad))


_FLOAT = static_type('float')


def format_int(value, spec):
    """int.__format__."""
    if str_len(spec) == 0:
        return to_str(value)
    parsed = parse_spec(value, spec, 'd', '>')
    fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty = parsed
    if ty in 'eEfFgG%':
        f = type_lookup(_FLOAT, '__format__')
        if f is MISSING:
            error(SystemError, 'float formatting arrives with K1f')
        return call_special(f, int_as_double(value), spec)
    if ty not in 'bcdoxXn':
        _unknown_type(ty, value)
    if precision != -1:
        error(ValueError, 'Precision not allowed in integer format specifier')
    if no_neg_0:
        error(ValueError, 'Negative zero coercion (z) not allowed in integer format specifier')
    if ty == 'c':
        if sign != '':
            error(ValueError, "Sign not allowed with integer format specifier 'c'")
        if alternate:
            error(ValueError, "Alternate form (#) not allowed with integer format specifier 'c'")
        if value > 9223372036854775807 or value < -9223372036854775808:
            error(OverflowError, 'Python int too large to convert to C long')
        if value < 0 or value > 0x10ffff:
            error(OverflowError, '%c arg not in range(0x110000)')
        return number_field('', '', '', str_chr(value), parsed, False)
    base = 2 if ty == 'b' else 8 if ty == 'o' else 16 if ty in 'xX' else 10
    prefix = ''
    if alternate and base != 10:
        prefix = '0b' if base == 2 else '0o' if base == 8 else '0x'
    digits = int_format(value, base, False)
    sign_char = ''
    if str_ord(digits, 0) == 45:
        sign_char = '-'
        digits = _sub(digits, 1, str_len(digits))
    return number_field(sign_char, prefix, digits, '', parsed, ty == 'X')


# str.format

def _auto_error(state, empty):
    if state == 2:
        if empty:
            error(ValueError, 'cannot switch from manual field specification to automatic field numbering')
    elif not empty:
        error(ValueError, 'cannot switch from automatic field numbering to manual field specification')


def _int_field(s, start, end):
    """get_integer of unicode_format.h: -1 when s[start:end] is not all digits."""
    if start >= end:
        return -1
    acc = 0
    i = start
    while i < end:
        d = str_decimal(s, i)
        if d < 0:
            return -1
        if acc > (_SSIZE_MAX - d) // 10:
            error(ValueError, 'Too many decimal digits in format string')
        acc = acc * 10 + d
        i += 1
    return acc


def _getitem(obj, key):
    if type(obj) is dict and type(key) is str:
        r = ns_get(obj, key, MISSING)
        if r is MISSING:
            error(KeyError, key)
        return r
    f = type_lookup(type(obj), '__getitem__')
    if f is MISSING:
        error(TypeError, _cat("'", _name(obj), "' object is not subscriptable"))
    return call_special(f, obj, key)


def _field_object(s, start, end, args, kwargs, auto):
    """get_field_object: the object a field name designates; auto is the
    [state, next number] list shared by all fields (state 0 init, 1 auto, 2 manual)."""
    i = start
    while i < end:
        c = str_ord(s, i)
        if c == 91 or c == 46:
            break
        i += 1
    first = _int_field(s, start, i)
    empty = start >= i
    numeric = empty or first != -1
    if numeric:
        if box_get(auto, 0) == 0:
            box_set(auto, 0, 1 if empty else 2)
        _auto_error(box_get(auto, 0), empty)
    if empty:
        first = box_get(auto, 1)
        box_set(auto, 1, first + 1)
    if first == -1:
        key = _sub(s, start, i)
        if kwargs is None:
            error(KeyError, key)
        obj = _getitem(kwargs, key)
    else:
        if args is None:
            error(ValueError, 'Format string contains positional fields')
        if first >= _tuple_size(args):
            error(IndexError, _cat('Replacement index ', int_format(first, 10, False),
                                   ' out of range for positional args tuple'))
        obj = _nth(args, first)
    while i < end:
        c = str_ord(s, i)
        i += 1
        if c == 46:
            j = i
            while i < end:
                d = str_ord(s, i)
                if d == 91 or d == 46:
                    break
                i += 1
            if j == i:
                error(ValueError, 'Empty attribute in format string')
            obj = getattr_hook(obj, _sub(s, j, i))
        elif c == 91:
            j = i
            seen = False
            while i < end:
                d = str_ord(s, i)
                i += 1
                if d == 93:
                    seen = True
                    break
            if not seen:
                error(ValueError, "Missing ']' in format string")
            name = _sub(s, j, i - 1)
            if j == i - 1:
                error(ValueError, 'Empty attribute in format string')
            n = _int_field(s, j, i - 1)
            obj = _getitem(obj, name if n == -1 else n)
        else:
            error(ValueError, "Only '.' or '[' may follow ']' in format field specifier")
    return obj


def _tuple_size(t):
    n = 0
    for _ in t:
        n += 1
    return n


def _nth(t, i):
    for x in t:
        if i == 0:
            return x
        i -= 1


def _convert(obj, conversion):
    if conversion == 'r':
        return to_repr(obj)
    if conversion == 's':
        return to_str(obj)
    if conversion == 'a':
        return to_ascii(obj)
    c = str_ord(conversion, 0)
    if c > 32 and c < 127:
        error(ValueError, _cat('Unknown conversion specifier ', conversion))
    error(ValueError, _cat('Unknown conversion specifier \\x', int_format(c, 16, False)))


def _build(s, start, end, args, kwargs, depth, auto):
    """build_string and do_markup: s[start:end] with its fields replaced."""
    if depth <= 0:
        error(ValueError, 'Max string recursion exceeded')
    out = []
    while start < end:
        lit = start
        c = 0
        markup = False
        while start < end:
            c = str_ord(s, start)
            start += 1
            if c == 123 or c == 125:
                markup = True
                break
        at_end = start >= end
        length = start - lit
        if markup and c == 125 and (at_end or str_ord(s, start) != 125):
            error(ValueError, "Single '}' encountered in format string")
        if markup and at_end and c == 123:
            error(ValueError, "Single '{' encountered in format string")
        if markup and not at_end:
            if str_ord(s, start) == c:
                start += 1
                markup = False
            else:
                length -= 1
        if length > 0:
            list_append(out, _sub(s, lit, lit + length))
        if not markup:
            continue
        name_start = start
        c = 0
        while start < end:
            c = str_ord(s, start)
            start += 1
            if c == 123:
                error(ValueError, "unexpected '{' in field name")
            if c == 91:
                while start < end and str_ord(s, start) != 93:
                    start += 1
                continue
            if c == 125 or c == 58 or c == 33:
                break
        name_end = start - 1
        conversion = ''
        spec_start = 0
        spec_end = 0
        expand = False
        if c == 33 or c == 58:
            fin = False
            if c == 33:
                if start >= end:
                    error(ValueError, 'end of string while looking for conversion specifier')
                conversion = str_item(s, start)
                start += 1
                if start < end:
                    c = str_ord(s, start)
                    start += 1
                    if c == 125:
                        fin = True
                    elif c != 58:
                        error(ValueError, "expected ':' after conversion specifier")
            if not fin:
                spec_start = start
                count = 1
                while start < end and count > 0:
                    c = str_ord(s, start)
                    start += 1
                    if c == 123:
                        expand = True
                        count += 1
                    elif c == 125:
                        count -= 1
                spec_end = start - 1
                if count != 0:
                    error(ValueError, "unmatched '{' in format spec")
        elif c != 125:
            error(ValueError, "expected '}' before end of string")
        obj = _field_object(s, name_start, name_end, args, kwargs, auto)
        if conversion != '':
            obj = _convert(obj, conversion)
        if expand:
            spec = _build(s, spec_start, spec_end, args, kwargs, depth - 1, auto)
        else:
            spec = _sub(s, spec_start, spec_end)
        list_append(out, format_value(obj, spec))
    return str_join('', (*out,))


def str_format(s, args, kwargs):
    """str.format(*args, **kwargs); args None for format_map."""
    return _build(s, 0, str_len(s), args, kwargs, 2, [0, 0])


# %-formatting

def _next_arg(ctx):
    i = box_get(ctx, 0)
    if i < box_get(ctx, 1):
        box_set(ctx, 0, i + 1)
        if box_get(ctx, 1) < 0:
            return box_get(ctx, 2)
        return _nth(box_get(ctx, 2), i)
    error(TypeError, 'not enough arguments for format string')


def _star(ctx):
    v = _next_arg(ctx)
    if type(v) is not int:
        if not kind(v, 'int'):
            error(TypeError, '* wants int')
    return v


def _is_number(v):
    return (type_lookup(type(v), '__index__') is not MISSING
            or type_lookup(type(v), '__int__') is not MISSING
            or type_lookup(type(v), '__float__') is not MISSING)


def _as_int(v, ty):
    """The int a %d, %x or %o conversion formats."""
    if kind(v, 'int'):
        return v
    if ty in 'diu':
        if _is_number(v):
            f = type_lookup(type(v), '__int__')
            if f is MISSING:
                f = type_lookup(type(v), '__index__')
            if f is MISSING:
                # a float without __int__: truncate through its own int
                f = type_lookup(_FLOAT, '__int__')
            r = call_special(f, v)
            if not kind(r, 'int'):
                error(TypeError, _cat('__int__ returned non-int (type ', _name(r), ')'))
            return r
        error(TypeError, _cat('%', ty, ' format: a real number is required, not ', _name(v)))
    if type_lookup(type(v), '__index__') is not MISSING:
        return index(v)
    error(TypeError, _cat('%', ty, ' format: an integer is required, not ', _name(v)))


def _float_spec(flags, width, precision, ty):
    spec = '<' if '-' in flags else ''
    if '+' in flags:
        spec = _cat(spec, '+')
    elif ' ' in flags:
        spec = _cat(spec, ' ')
    if '#' in flags:
        spec = _cat(spec, '#')
    if '0' in flags and '-' not in flags:
        spec = _cat(spec, '0')
    if width >= 0:
        spec = _cat(spec, int_format(width, 10, False))
    return _cat(spec, '.', int_format(precision, 10, False), ty)


def _is_flag(c):
    return c == 45 or c == 43 or c == 32 or c == 35 or c == 48


def _is_length(c):
    return c == 104 or c == 108 or c == 76


def pct_format(fmt, args):
    """PyUnicode_Format(fmt, args)."""
    n = str_len(fmt)
    out = []
    ctx = [0, -1, args]
    mapping = None
    if type(args) is tuple:
        box_set(ctx, 0, 0)
        box_set(ctx, 1, _tuple_size(args))
    else:
        box_set(ctx, 0, -2)
        if ((kind(args, 'dict') or type_lookup(type(args), '__getitem__') is not MISSING)
                and not kind(args, 'tuple') and not kind(args, 'str')):
            mapping = args
    i = 0
    while i < n:
        j = str_find(fmt, '%', i, n, False)
        if j < 0:
            list_append(out, _sub(fmt, i, n))
            break
        if j > i:
            list_append(out, _sub(fmt, i, j))
        i = j + 1
        if i < n and str_ord(fmt, i) == 37:
            list_append(out, '%')
            i += 1
            continue
        flags = ''
        width = -1
        precision = -1
        key = MISSING
        if i < n and str_ord(fmt, i) == 40:
            depth = 1
            k = i + 1
            while k < n and depth > 0:
                c = str_ord(fmt, k)
                if c == 41:
                    depth -= 1
                elif c == 40:
                    depth += 1
                k += 1
            if depth > 0:
                error(ValueError, 'incomplete format key')
            if mapping is None:
                error(TypeError, 'format requires a mapping')
            key = _getitem(mapping, _sub(fmt, i + 1, k - 1))
            i = k
        while i < n and _is_flag(str_ord(fmt, i)):
            flags = _cat(flags, str_item(fmt, i))
            i += 1
        if i < n and str_ord(fmt, i) == 42:
            width = _star(ctx)
            if width < 0:
                flags = _cat(flags, '-')
                width = -width
            i += 1
        else:
            width = -1
            while i < n and str_decimal(fmt, i) >= 0:
                if width == -1:
                    width = 0
                width = width * 10 + str_decimal(fmt, i)
                if width > _SSIZE_MAX:
                    error(ValueError, 'width too big')
                i += 1
        if i < n and str_ord(fmt, i) == 46:
            i += 1
            if i < n and str_ord(fmt, i) == 42:
                precision = _star(ctx)
                if precision < 0:
                    precision = 0
                i += 1
            else:
                precision = 0
                while i < n and str_decimal(fmt, i) >= 0:
                    precision = precision * 10 + str_decimal(fmt, i)
                    if precision > _SSIZE_MAX:
                        error(ValueError, 'precision too big')
                    i += 1
        while i < n and _is_length(str_ord(fmt, i)):
            i += 1
        if i >= n:
            error(ValueError, 'incomplete format')
        ty = str_item(fmt, i)
        i += 1
        v = key if key is not MISSING else _next_arg(ctx)
        list_append(out, _convert_arg(ty, v, flags, width, precision, fmt, i))
    if box_get(ctx, 0) < box_get(ctx, 1) and mapping is None:
        error(TypeError, 'not all arguments converted during string formatting')
    return str_join('', (*out,))


def _justify(body, flags, width):
    n = str_len(body)
    if width <= n:
        return body
    if '-' in flags:
        return _cat(body, _pad(' ', width - n))
    return _cat(_pad(' ', width - n), body)


def _convert_arg(ty, v, flags, width, precision, fmt, i):
    if ty in 'sra':
        s = to_str(v) if ty == 's' else to_repr(v) if ty == 'r' else to_ascii(v)
        if precision >= 0 and str_len(s) > precision:
            s = _sub(s, 0, precision)
        return _justify(s, flags, width)
    if ty == 'c':
        if kind(v, 'str'):
            if str_len(v) != 1:
                error(TypeError, _cat('%c requires an int or a unicode character, not a string of length ',
                                      int_format(str_len(v), 10, False)))
            return _justify(_sub(v, 0, 1), flags, width)
        if not kind(v, 'int') and type_lookup(type(v), '__index__') is MISSING:
            error(TypeError, _cat('%c requires an int or a unicode character, not ', _name(v)))
        c = index(v)
        if c < 0 or c > 0x10ffff:
            error(OverflowError, '%c arg not in range(0x110000)')
        return _justify(str_chr(c), flags, width)
    if ty in 'diuxXo':
        num = _as_int(v, ty)
        base = 8 if ty == 'o' else 16 if ty in 'xX' else 10
        digits = int_format(num, base, False)
        sign = ''
        if str_ord(digits, 0) == 45:
            sign = '-'
            digits = _sub(digits, 1, str_len(digits))
        elif '+' in flags:
            sign = '+'
        elif ' ' in flags:
            sign = ' '
        prefix = ''
        if '#' in flags:
            prefix = '0o' if ty == 'o' else '0x' if ty == 'x' else '0X' if ty == 'X' else ''
        if ty == 'X':
            digits = _upper(digits)
        if precision >= 0 and str_len(digits) < precision:
            digits = _cat(_pad('0', precision - str_len(digits)), digits)
        head = _cat(sign, prefix)
        room = width - str_len(head) - str_len(digits)
        if room > 0:
            if '-' in flags:
                return _cat(head, digits, _pad(' ', room))
            if '0' in flags:
                return _cat(head, _pad('0', room), digits)
            return _cat(_pad(' ', room), head, digits)
        return _cat(head, digits)
    if ty in 'eEfFgG':
        f = type_lookup(_FLOAT, '__format__')
        if f is MISSING:
            error(SystemError, 'float formatting arrives with K1f')
        ff = type_lookup(type(v), '__float__')
        x = v
        if kind(v, 'int'):
            x = int_as_double(v)
        elif ff is not MISSING:
            x = call_special(ff, v)
        else:
            error(TypeError, _cat('must be real number, not ', _name(v)))
        return call_special(f, x, _float_spec(flags, width, precision if precision >= 0 else 6, ty))
    c = str_ord(ty, 0)
    error(ValueError, _cat("unsupported format character '", ty, "' (0x", int_format(c, 16, False),
                           ') at index ', int_format(i - 1, 10, False)))


@_boot.kernel_type(static_type('object'))
class object:
    def __format__(self, format_spec, /):
        if not kind(format_spec, 'str'):
            error(TypeError, _cat('__format__() argument must be str, not ', _name(format_spec)))
        return object_format(self, format_spec)
