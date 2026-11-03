"""BUILD_TEMPLATE and BUILD_INTERPOLATION: the t-string types are the builtin
module _templatelib's, imported when the first t-string is evaluated."""

from _lucifer import kernel_set

__all__ = ()

_CONVERSIONS = (None, 's', 'r', 'a')


def build_template(strings, interpolations):
    import _templatelib
    return _templatelib._build(strings, interpolations)


def build_interpolation(value, expression, conversion, format_spec):
    import _templatelib
    return _templatelib.Interpolation(value, expression, _CONVERSIONS[conversion], format_spec)


kernel_set('build_template', build_template)
kernel_set('build_interpolation', build_interpolation)
