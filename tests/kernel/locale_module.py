import _locale
import locale

names = sorted(n for n in dir(_locale) if n.isupper())
print(names, [getattr(_locale, n) for n in names])
print(_locale.Error.__module__, issubclass(_locale.Error, Exception))
print(_locale.setlocale(_locale.LC_ALL, 'C'), _locale.localeconv()['decimal_point'])
for name in ('C', 'POSIX', 'fr_FR.UTF-8', 'de_DE.UTF-8', 'no_SUCH.locale'):
    try:
        print(name, _locale.setlocale(_locale.LC_ALL, name))
    except _locale.Error as e:
        print(name, 'Error', e)
        continue
    conv = _locale.localeconv()
    print(conv['decimal_point'], repr(conv['thousands_sep']), conv['grouping'], conv['frac_digits'])
    print(_locale.nl_langinfo(_locale.DAY_1), _locale.nl_langinfo(_locale.RADIXCHAR), _locale.nl_langinfo(_locale.D_FMT))
    print(_locale.strcoll('a', 'b'), _locale.strcoll('b', 'a'), _locale.strcoll('a', 'a'), repr(_locale.strxfrm('abc')))
    print(locale.format_string('%.2f', 1234.5, grouping=True), locale.atof('1,5' if name.startswith(('fr', 'de')) else '1.5'))
_locale.setlocale(_locale.LC_ALL, 'C')
for bad in (lambda: _locale.setlocale(_locale.LC_ALL, 5), lambda: _locale.nl_langinfo(12345),
            lambda: _locale.strcoll('a', b'b'), lambda: _locale.strxfrm(1), lambda: _locale.setlocale(99),
            lambda: _locale.strcoll('a\0b', 'a'), lambda: _locale.strxfrm('a\0b'),
            lambda: _locale.setlocale(_locale.LC_ALL, 'C\0x'), lambda: _locale.gettext('a\0b')):
    try:
        bad()
    except (TypeError, ValueError, _locale.Error) as e:
        print(type(e).__name__, e)
print(_locale.gettext('hello'), _locale.dgettext('nodomain', 'hi'), _locale.textdomain(None))
