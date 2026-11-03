"""The t-string types (Objects/templateobject.c, Objects/interpolationobject.c).
Both print and pickle as string.templatelib's and accept no subclasses."""

__all__ = ('Template', 'Interpolation')

_MODULE = 'string.templatelib'


def _generic_alias(cls, item):
    import types
    return types.GenericAlias(cls, item)


def _final(cls):
    raise TypeError("type '%s.%s' is not an acceptable base type" % (_MODULE, cls.__name__))


class Interpolation:
    """Interpolation object"""
    __slots__ = ('_value', '_expression', '_conversion', '_format_spec')
    __match_args__ = ('value', 'expression', 'conversion', 'format_spec')

    def __init_subclass__(cls):
        _final(Interpolation)

    def __new__(cls, value, expression='', conversion=None, format_spec=''):
        if not isinstance(expression, str):
            raise TypeError("Interpolation() argument 'expression' must be str, not %s"
                            % type(expression).__name__)
        if conversion is not None:
            if not isinstance(conversion, str):
                raise TypeError("Interpolation() argument 'conversion' must be str, not %s"
                                % type(conversion).__name__)
            if conversion not in ('a', 'r', 's'):
                raise ValueError("Interpolation() argument 'conversion' must be one of 's', 'a' or 'r'")
        if not isinstance(format_spec, str):
            raise TypeError("Interpolation() argument 'format_spec' must be str, not %s"
                            % type(format_spec).__name__)
        self = object.__new__(cls)
        self._value, self._expression = value, expression
        self._conversion, self._format_spec = conversion, format_spec
        return self

    value = property(lambda self: self._value, doc='Value')
    expression = property(lambda self: self._expression, doc='Expression')
    conversion = property(lambda self: self._conversion, doc='Conversion')
    format_spec = property(lambda self: self._format_spec, doc='Format specifier')

    def __repr__(self):
        return 'Interpolation(%r, %r, %r, %r)' % (
            self._value, self._expression, self._conversion, self._format_spec)

    def __reduce__(self):
        return type(self), (self._value, self._expression, self._conversion, self._format_spec)

    __class_getitem__ = classmethod(_generic_alias)


class Template:
    """Template object"""
    __slots__ = ('_strings', '_interpolations')

    def __init_subclass__(cls):
        _final(Template)

    def __new__(cls, *args, **kwargs):
        if kwargs:
            raise TypeError('Template.__new__ only accepts *args arguments')
        strings, interpolations, last_was_str = [], [], False
        for item in args:
            if isinstance(item, str):
                if last_was_str:
                    strings[-1] += item
                else:
                    strings.append(item)
                last_was_str = True
            elif type(item) is Interpolation:
                if not last_was_str:
                    strings.append('')
                interpolations.append(item)
                last_was_str = False
            else:
                raise TypeError("Template.__new__ *args need to be of type 'str' or "
                                "'Interpolation', got %s" % type(item).__name__)
        if not last_was_str:
            strings.append('')
        return _build(tuple(strings), tuple(interpolations))

    strings = property(lambda self: self._strings, doc='Strings')
    interpolations = property(lambda self: self._interpolations, doc='Interpolations')

    @property
    def values(self):
        """Values of interpolations"""
        return tuple(i._value for i in self._interpolations)

    def __iter__(self):
        for string, interpolation in zip(self._strings, self._interpolations + (None,)):
            if string:
                yield string
            if interpolation is not None:
                yield interpolation

    def __repr__(self):
        return 'Template(strings=%r, interpolations=%r)' % (self._strings, self._interpolations)

    def __add__(self, other):
        if type(other) is not Template:
            raise TypeError('can only concatenate string.templatelib.Template (not "%s") '
                            'to string.templatelib.Template' % type(other).__name__)
        strings = self._strings[:-1] + (self._strings[-1] + other._strings[0],) + other._strings[1:]
        return _build(strings, self._interpolations + other._interpolations)

    def __reduce__(self):
        import string.templatelib
        return string.templatelib._template_unpickle, (self._strings, self._interpolations)

    __class_getitem__ = classmethod(_generic_alias)


def _build(strings, interpolations):
    self = object.__new__(Template)
    self._strings, self._interpolations = strings, interpolations
    return self


Interpolation.__module__ = Template.__module__ = _MODULE
