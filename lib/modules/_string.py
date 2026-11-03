"""_string (Objects/stringlib/unicode_format.h): the pieces of str.format's
mini-language that string.Formatter uses."""


def formatter_field_name_split(field_name, /):
    """Split the argument as a field name.

Returns the first part, as an int when it is all digits, and an iterator
of (is_attribute, key) pairs for the rest."""
    first = []
    i = 0
    n = len(field_name)
    while i < n and field_name[i] not in '.[':
        first.append(field_name[i])
        i += 1
    first = ''.join(first)
    if first.isdigit() and first.isascii():
        first = int(first)
    return first, _rest(field_name, i)


def _rest(field_name, i):
    n = len(field_name)
    while i < n:
        c = field_name[i]
        i += 1
        if c == '.':
            start = i
            while i < n and field_name[i] not in '.[':
                i += 1
            if i == start:
                raise ValueError('Empty attribute in format string')
            yield True, field_name[start:i]
        elif c == '[':
            start = i
            while i < n and field_name[i] != ']':
                i += 1
            if i >= n:
                raise ValueError("Missing ']' in format string")
            key = field_name[start:i]
            i += 1
            if not key:
                raise ValueError('Empty attribute in format string')
            yield False, int(key) if key.isdigit() and key.isascii() else key
        else:
            raise ValueError('Only \'.\' or \'[\' may follow \']\' in format field specifier')


def formatter_parser(format_string, /):
    """Parse a format string into (literal, field_name, format_spec, conversion)
tuples, as str.format does."""
    if not isinstance(format_string, str):
        raise TypeError("expected str, got %s" % type(format_string).__name__)
    return _parse(format_string)


def _parse(s):
    i = 0
    n = len(s)
    literal = []
    while i < n:
        c = s[i]
        if c in '{}':
            if i + 1 < n and s[i + 1] == c:
                literal.append(c)
                i += 2
                continue
            if c == '}':
                raise ValueError("Single '}' encountered in format string")
            i += 1
            depth = 1
            start = i
            while i < n and depth:
                if s[i] == '{':
                    depth += 1
                elif s[i] == '}':
                    depth -= 1
                i += 1
            if depth:
                raise ValueError("expected '}' before end of string" if start < n
                                 else "Single '{' encountered in format string")
            field = s[start:i - 1]
            name, conversion, spec = _split_field(field)
            yield ''.join(literal), name, spec, conversion
            literal = []
        else:
            literal.append(c)
            i += 1
    if literal:
        yield ''.join(literal), None, None, None


def _split_field(field):
    """name!conversion:spec, where the name may hold [] with colons and bangs."""
    i = 0
    n = len(field)
    bracket = False
    while i < n:
        c = field[i]
        if c == '[':
            bracket = True
        elif c == ']':
            bracket = False
        elif not bracket and c in '!:':
            break
        i += 1
    name = field[:i]
    conversion = None
    spec = ''
    if i < n and field[i] == '!':
        if i + 1 >= n:
            raise ValueError('end of string while looking for conversion specifier')
        conversion = field[i + 1]
        i += 2
        if i < n and field[i] != ':':
            raise ValueError("expected ':' after conversion specifier")
    if i < n and field[i] == ':':
        spec = field[i + 1:]
    return name, conversion, spec
