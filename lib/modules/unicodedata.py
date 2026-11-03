"""unicodedata (Modules/unicodedata.c): argument handling and error
messages over the database lookups of runtime/prim/ucd.c. ucd_3_2_0 answers
from the Unicode 3.2.0 database, which IDNA uses."""

import _lucifer

unidata_version = _lucifer.ucd_version()

_MISSING = object()


def _functions(old):
    def check(name, value):
        if not isinstance(value, str):
            raise TypeError('%s() argument must be a unicode character, not %s'
                            % (name, type(value).__name__))
        if len(value) != 1:
            raise TypeError('%s(): argument must be a unicode character, not a string of length %d'
                            % (name, len(value)))
        return ord(value)

    def decimal(chr, default=_MISSING, /):
        """Converts a Unicode character into its equivalent decimal value.

Returns the decimal value assigned to the character chr as integer.
If no such value is defined, default is returned, or, if not given,
ValueError is raised."""
        value = _lucifer.ucd_decimal(old, check('decimal', chr))
        if value < 0:
            if default is _MISSING:
                raise ValueError('not a decimal')
            return default
        return value

    def digit(chr, default=_MISSING, /):
        """Converts a Unicode character into its equivalent digit value.

Returns the digit value assigned to the character chr as integer.
If no such value is defined, default is returned, or, if not given,
ValueError is raised."""
        value = _lucifer.ucd_digit(old, check('digit', chr))
        if value < 0:
            if default is _MISSING:
                raise ValueError('not a digit')
            return default
        return value

    def numeric(chr, default=_MISSING, /):
        """Converts a Unicode character into its equivalent numeric value.

Returns the numeric value assigned to the character chr as float.
If no such value is defined, default is returned, or, if not given,
ValueError is raised."""
        value = _lucifer.ucd_numeric(old, check('numeric', chr))
        if value is None:
            if default is _MISSING:
                raise ValueError('not a numeric character')
            return default
        return value

    def category(chr, /):
        """Returns the general category assigned to the character chr as string."""
        return _lucifer.ucd_category(old, check('category', chr))

    def bidirectional(chr, /):
        """Returns the bidirectional class assigned to the character chr as string.

If no such value is defined, an empty string is returned."""
        return _lucifer.ucd_bidirectional(old, check('bidirectional', chr))

    def combining(chr, /):
        """Returns the canonical combining class assigned to the character chr as integer.

Returns 0 if no combining class is defined."""
        return _lucifer.ucd_combining(old, check('combining', chr))

    def mirrored(chr, /):
        """Returns the mirrored property assigned to the character chr as integer.

Returns 1 if the character has been identified as a "mirrored"
character in bidirectional text, 0 otherwise."""
        return _lucifer.ucd_mirrored(old, check('mirrored', chr))

    def east_asian_width(chr, /):
        """Returns the east asian width assigned to the character chr as string."""
        return _lucifer.ucd_east_asian_width(old, check('east_asian_width', chr))

    def decomposition(chr, /):
        """Returns the character decomposition mapping assigned to the character chr as string.

An empty string is returned in case no such mapping is defined."""
        return _lucifer.ucd_decomposition(old, check('decomposition', chr))

    def name(chr, default=_MISSING, /):
        """Returns the name assigned to the character chr as a string.

If no name is defined, default is returned, or, if not given,
ValueError is raised."""
        value = _lucifer.ucd_name(old, check('name', chr))
        if value is None:
            if default is _MISSING:
                raise ValueError('no such name')
            return default
        return value

    def lookup(name, /):
        """Look up character by name.

If a character with the given name is found, return the
corresponding character.  If not found, KeyError is raised."""
        if not isinstance(name, str):
            if isinstance(name, (bytes, bytearray, memoryview)):
                name = bytes(name).decode('latin-1')
            else:
                raise TypeError("a bytes-like object is required, not '%s'" % type(name).__name__)
        if len(name) > 256:
            raise KeyError('name too long')
        value = _lucifer.ucd_lookup(old, name)
        if value is None:
            raise KeyError("undefined character name '%s'" % name)
        return value

    def is_normalized(*args):
        """Return whether the Unicode string unistr is in the normal form 'form'.

Valid values for form are 'NFC', 'NFKC', 'NFD', and 'NFKD'."""
        form, unistr = _two('is_normalized', args)
        _unicode(form, 'is_normalized', 1)
        _unicode(unistr, 'is_normalized', 2)
        return _lucifer.ucd_is_normalized(old, form, unistr)

    def normalize(*args):
        """Return the normal form 'form' for the Unicode string unistr.

Valid values for form are 'NFC', 'NFKC', 'NFD', and 'NFKD'."""
        form, unistr = _two('normalize', args)
        _unicode(form, 'normalize', 1)
        _unicode(unistr, 'normalize', 2)
        return _lucifer.ucd_normalize(old, form, unistr)

    return (decimal, digit, numeric, category, bidirectional, combining, mirrored, east_asian_width,
            decomposition, name, lookup, is_normalized, normalize)


def _two(function, args):
    if len(args) != 2:
        raise TypeError('%s expected 2 arguments, got %d' % (function, len(args)))
    return args


def _unicode(value, function, position):
    if not isinstance(value, str):
        raise TypeError('%s() argument %d must be str, not %s' % (function, position, type(value).__name__))


(decimal, digit, numeric, category, bidirectional, combining, mirrored, east_asian_width,
 decomposition, name, lookup, is_normalized, normalize) = _functions(False)


class UCD:
    """The Unicode database of an earlier version."""
    __slots__ = ('unidata_version',) + tuple(f.__name__ for f in _functions(True))

    def __new__(cls, *args, **kwargs):
        raise TypeError("cannot create 'unicodedata.UCD' instances")

    def __init_subclass__(cls):
        raise TypeError("type 'unicodedata.UCD' is not an acceptable base type")


def _previous(version):
    self = object.__new__(UCD)
    for function in _functions(True):
        object.__setattr__(self, function.__name__, function)
    object.__setattr__(self, 'unidata_version', version)
    return self


UCD.__module__ = 'unicodedata'
ucd_3_2_0 = _previous('3.2.0')
