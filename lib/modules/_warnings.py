"""_warnings: warn() for importlib's bootstrap and warn_explicit() for the
compiler, with the default filters and no others; the warnings module,
once it can be imported, does the rest. Warnings that the default filters
ignore are dropped and the others go to standard error. Once the warnings
module is imported, its Python implementation decides."""

import _lucifer
import sys

_IGNORED = ('DeprecationWarning', 'PendingDeprecationWarning', 'ImportWarning', 'ResourceWarning')

# (action, category, module): Python/_warnings.c's default filters.
filters = [
    ('default', DeprecationWarning, '__main__'),
    ('ignore', DeprecationWarning, None),
    ('ignore', PendingDeprecationWarning, None),
    ('ignore', ImportWarning, None),
    ('ignore', ResourceWarning, None),
]
_defaultaction = 'default'
_onceregistry = {}


def _action(category, module):
    for action, cat, mod in filters:
        if issubclass(category, cat) and (mod is None or mod == module):
            return action
    return _defaultaction


def _source_line(filename, lineno):
    try:
        with open(filename, encoding='utf-8') as f:
            for number, line in enumerate(f, 1):
                if number == lineno:
                    return line.strip()
    except (OSError, ValueError):
        pass
    return ''


def _show(message, category, filename, lineno):
    text = '%s:%s: %s: %s\n' % (filename, lineno, category.__name__, message)
    line = _source_line(filename, lineno)
    if line:
        text += '  ' + line + '\n'
    stream = sys.stderr
    if stream is None:
        _lucifer.write(2, text)
    else:
        stream.write(text)


def warn_explicit(message, category, filename, lineno, module=None, registry=None,
                  module_globals=None, source=None):
    """Issue a warning, or maybe ignore it or raise an exception."""
    imported = sys.modules.get('warnings')
    if imported is not None:
        return imported.warn_explicit(message, category, filename, lineno, module, registry,
                                      module_globals, source)
    if module is None:
        module = filename or '<unknown>'
        if module[-3:].lower() == '.py':
            module = module[:-3]
    if isinstance(message, Warning):
        text = str(message)
        category = type(message)
    else:
        text = message
        message = category(message)
    key = (text, category, lineno)
    if registry is not None and registry.get(key):
        return
    action = _action(category, module)
    if action == 'error':
        raise message
    if action == 'ignore':
        return
    if action == 'once':
        if registry is not None:
            registry[key] = 1
        if (text, category) in _onceregistry:
            return
        _onceregistry[(text, category)] = 1
    elif action == 'module':
        if registry is not None:
            registry[key] = 1
            if registry.get((text, category, 0)):
                return
            registry[(text, category, 0)] = 1
    elif action == 'default':
        if registry is not None:
            registry[key] = 1
    elif action != 'always':
        raise RuntimeError('Unrecognized action (%r) in warnings.filters' % (action,))
    _show(text, category, filename, lineno)


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
