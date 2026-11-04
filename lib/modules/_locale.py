"""_locale (Modules/_localemodule.c): the process locale, over libc's setlocale,
localeconv, wcscoll, wcsxfrm, nl_langinfo and gettext, called through the
foreign call primitive."""

import _lucifer

CHAR_MAX = 127
LC_CTYPE, LC_NUMERIC, LC_TIME, LC_COLLATE, LC_MONETARY, LC_MESSAGES, LC_ALL = 0, 1, 2, 3, 4, 5, 6

# langinfo.h: (category << 16) | index
CODESET = 14
(ABDAY_1, ABDAY_2, ABDAY_3, ABDAY_4, ABDAY_5, ABDAY_6, ABDAY_7,
 DAY_1, DAY_2, DAY_3, DAY_4, DAY_5, DAY_6, DAY_7) = range(0x20000, 0x2000e)
(ABMON_1, ABMON_2, ABMON_3, ABMON_4, ABMON_5, ABMON_6, ABMON_7, ABMON_8, ABMON_9, ABMON_10, ABMON_11,
 ABMON_12, MON_1, MON_2, MON_3, MON_4, MON_5, MON_6, MON_7, MON_8, MON_9, MON_10, MON_11, MON_12,
 AM_STR, PM_STR, D_T_FMT, D_FMT, T_FMT, T_FMT_AMPM, ERA) = range(0x2000e, 0x2002d)
ERA_D_FMT, ALT_DIGITS, ERA_D_T_FMT, ERA_T_FMT = 0x2002e, 0x2002f, 0x20030, 0x20031
_DATE_FMT = 0x2006c
RADIXCHAR, THOUSEP = 0x10000, 0x10001
CRNCYSTR = 0x4000f
YESEXPR, NOEXPR = 0x50000, 0x50001
_langinfo_keys = frozenset(n for k, n in list(globals().items())
                      if isinstance(n, int) and k.isupper() and k not in ('CHAR_MAX',) and not k.startswith('LC_')) \
    | {_DATE_FMT}


class Error(Exception):
    """locale.Error"""
    __module__ = 'locale'


_libc = _lucifer.ct_dlopen(None, 2)


def _fn(name):
    return _lucifer.ct_dlsym(_libc, name)


_setlocale, _localeconv, _nl_langinfo = _fn('setlocale'), _fn('localeconv'), _fn('nl_langinfo')
_wcscoll, _wcsxfrm = _fn('wcscoll'), _fn('wcsxfrm')
_gettext, _dgettext, _dcgettext = _fn('gettext'), _fn('dgettext'), _fn('dcgettext')
_textdomain, _bindtextdomain = _fn('textdomain'), _fn('bindtextdomain')
_bind_codeset = _fn('bind_textdomain_codeset')


def _call(function, rtype, types, args):
    return _lucifer.ct_call(function, ord(rtype), tuple(ord(t) for t in types), tuple(args), 0)


def _text(address):
    """The zero-terminated string at address, decoded as the locale encoding."""
    if not address:
        return None
    return _lucifer.ct_read(address, _lucifer.ct_strlen(address, False)).decode('utf-8', 'surrogateescape')


def _no_nul(text):
    if '\0' in text:
        raise ValueError('embedded null character')


def _cstring(text):
    """(buffer, address) of text as a zero-terminated string; keep the buffer alive."""
    _no_nul(text)
    data = text.encode('utf-8', 'surrogateescape') + b'\0'
    return data, _lucifer.ct_address(data)


def _aligned(size):
    """(buffer, address) of size zeroed bytes at an address that wchar_t may be read from."""
    buffer = bytearray(size + 4)
    address = _lucifer.ct_address(buffer)
    return buffer, address, -address % 4


def _wide(text):
    _no_nul(text)
    raw = text.encode('utf-32-le', 'surrogatepass') + b'\0\0\0\0'
    buffer, address, skip = _aligned(len(raw))
    buffer[skip:skip + len(raw)] = raw
    return buffer, address + skip


def setlocale(category, locale=None, /):
    """Activates/queries locale processing."""
    if locale is None:
        result = _call(_setlocale, 'P', 'iP', (category, 0))
        if not result:
            raise Error('locale query failed')
        return _text(result)
    if not isinstance(locale, str):
        raise TypeError('setlocale() argument 2 must be str or None, not ' + type(locale).__name__)
    data, address = _cstring(locale)
    result = _call(_setlocale, 'P', 'iP', (category, address))
    if not result:
        raise Error('unsupported locale setting')
    return _text(result)


def _grouping(address):
    """copy_grouping: group sizes up to CHAR_MAX; a trailing 0 repeats the last size forever."""
    data = _lucifer.ct_read(address, _lucifer.ct_strlen(address, False)) if address else b''
    groups = []
    for byte in data:
        if byte == CHAR_MAX:
            return groups
        groups.append(byte)
    if data:
        groups.append(0)
    return groups


def localeconv():
    """Returns numeric and monetary locale-specific parameters."""
    lc = _call(_localeconv, 'P', '', ())
    pointers = _lucifer.ct_read(lc, 80)
    chars = _lucifer.ct_read(lc + 80, 14)
    at = lambda i: int.from_bytes(pointers[8 * i:8 * i + 8], 'little')
    signed = lambda byte: byte - 256 if byte > 127 else byte
    result = {
        'int_curr_symbol': _text(at(3)), 'currency_symbol': _text(at(4)),
        'mon_decimal_point': _text(at(5)), 'mon_thousands_sep': _text(at(6)),
        'mon_grouping': _grouping(at(7)), 'positive_sign': _text(at(8)), 'negative_sign': _text(at(9)),
    }
    for name, byte in zip(('int_frac_digits', 'frac_digits', 'p_cs_precedes', 'p_sep_by_space', 'n_cs_precedes',
                           'n_sep_by_space', 'p_sign_posn', 'n_sign_posn'), chars):
        result[name] = signed(byte)
    result['decimal_point'] = _text(at(0))
    result['thousands_sep'] = _text(at(1))
    result['grouping'] = _grouping(at(2))
    return result


def strcoll(os1, os2, /):
    """Compares two strings according to the locale."""
    for n, arg in enumerate((os1, os2), 1):
        if not isinstance(arg, str):
            raise TypeError('strcoll() argument %d must be str, not %s' % (n, type(arg).__name__))
    a, pa = _wide(os1)
    b, pb = _wide(os2)
    return _call(_wcscoll, 'i', 'PP', (pa, pb))


def strxfrm(string, /):
    """Return a string that can be used as a key for locale-aware comparisons."""
    if not isinstance(string, str):
        raise TypeError('strxfrm() argument must be str, not ' + type(string).__name__)
    data, address = _wide(string)
    size = _call(_wcsxfrm, 'L', 'PPL', (0, address, 0))
    out, out_address, skip = _aligned((size + 1) * 4)
    _call(_wcsxfrm, 'L', 'PPL', (out_address + skip, address, size + 1))
    return bytes(out[skip:skip + size * 4]).decode('utf-32-le', 'surrogatepass')


def nl_langinfo(key, /):
    """Return the value for the locale information associated with key."""
    if key not in _langinfo_keys:
        raise ValueError('unsupported langinfo constant')
    return _text(_call(_nl_langinfo, 'P', 'i', (key,)))


def getencoding():
    """Get the current locale encoding."""
    codeset = nl_langinfo(CODESET)
    return 'UTF-8' if codeset == 'ANSI_X3.4-1968' else codeset


def _string_call(function, rtype, args):
    """Calls function with each str of args as a C string (None as NULL)."""
    keep = [_cstring(a) if isinstance(a, str) else None for a in args]
    addresses = [k[1] if k else 0 for k in keep]
    return _text(_call(function, rtype, 'P' * len(addresses), addresses))


def gettext(msg, /):
    """gettext(msg) -> string"""
    return _string_call(_gettext, 'P', (msg,))


def dgettext(domain, msg, /):
    """dgettext(domain, msg) -> string"""
    return _string_call(_dgettext, 'P', (domain, msg))


def dcgettext(domain, msg, category, /):
    """dcgettext(domain, msg, category) -> string"""
    keep = [_cstring(a) if isinstance(a, str) else None for a in (domain, msg)]
    addresses = [k[1] if k else 0 for k in keep]
    return _text(_call(_dcgettext, 'P', 'PPi', (*addresses, category)))


def textdomain(domain, /):
    """Set the C library's textdmain to domain, returning the new domain."""
    return _string_call(_textdomain, 'P', (domain,))


def bindtextdomain(domain, dir, /):
    """Bind the C library's domain to dir."""
    if not domain:
        raise ValueError('domain must not be empty')
    return _string_call(_bindtextdomain, 'P', (domain, dir))


def bind_textdomain_codeset(domain, codeset, /):
    """Bind the C library's domain to codeset."""
    if not domain:
        raise ValueError('domain must not be empty')
    return _string_call(_bind_codeset, 'P', (domain, codeset))
