"""_warnings: only warn(), for importlib's bootstrap. Warnings of the
categories the default filters ignore are dropped and the others go to
stderr; the warnings module, once it can be imported, does the rest."""

import _lucifer

_IGNORED = ('DeprecationWarning', 'PendingDeprecationWarning', 'ImportWarning', 'ResourceWarning')


def warn(message, category=None, stacklevel=1, source=None, *, skip_file_prefixes=()):
    """Issue a warning, or maybe ignore it or raise an exception."""
    if isinstance(message, Warning):
        category = type(message)
    elif category is None:
        category = UserWarning
    if category.__name__ in _IGNORED:
        return
    _lucifer.write(2, '<sys>:0: %s: %s\n' % (category.__name__, message))
