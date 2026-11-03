"""_warnings: warn() for the interpreter's own callers (importlib's bootstrap).
Before the warnings module is imported, warnings of the categories the default
filters ignore are dropped and the others go to stderr; afterwards the
warnings module's Python implementation decides."""

import _lucifer
import sys

_IGNORED = ('DeprecationWarning', 'PendingDeprecationWarning', 'ImportWarning', 'ResourceWarning')


def warn(message, category=None, stacklevel=1, source=None, *, skip_file_prefixes=()):
    """Issue a warning, or maybe ignore it or raise an exception."""
    module = sys.modules.get('warnings')
    if module is not None:
        return module.warn(message, category, stacklevel + 1, source,
                           skip_file_prefixes=skip_file_prefixes)
    if isinstance(message, Warning):
        category = type(message)
    elif category is None:
        category = UserWarning
    if category.__name__ in _IGNORED:
        return
    _lucifer.write(2, '<sys>:0: %s: %s\n' % (category.__name__, message))
