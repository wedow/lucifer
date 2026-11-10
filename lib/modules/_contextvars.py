"""_contextvars (Modules/_contextvarsmodule.c, Python/context.c): ContextVar,
Context and Token. A context holds its variables in a dict that is replaced,
never changed, when a variable is set, so a copy shares it until either side
sets. The current context is the task's (_lucifer.context_get/context_set),
made empty on first use."""

import _lucifer


def _get_current():
    context = _lucifer.context_get()
    if context is None:
        context = Context()
        _lucifer.context_set(context, False)
    return context


class _Missing:
    def __repr__(self):
        return '<Token.MISSING>'


_MISSING = _Missing()
_ABSENT = object()       # no value in the context: Token.MISSING may be one


def _generic_alias(cls, item):
    import types
    return types.GenericAlias(cls, item)


class Context:
    """Context: a mapping of context variables to their values."""

    def __init__(self):
        self._data = {}
        self._entered = False

    def copy(self):
        """Return a shallow copy of the context object."""
        new = Context()
        new._data = self._data
        return new

    def run(self, callable, /, *args, **kwargs):
        """Execute callable(*args, **kwargs) code in the context object."""
        if self._entered:
            raise RuntimeError('cannot enter context: %r is already entered' % (self,))
        self._entered = True
        prev = _lucifer.context_set(self, True)
        try:
            return callable(*args, **kwargs)
        finally:
            _lucifer.context_set(prev, True)
            self._entered = False

    def get(self, key, default=None, /):
        """Return the value for `key` if `key` has the value in the context object."""
        _check_var(key)
        return self._data.get(key, default)

    def __getitem__(self, key):
        _check_var(key)
        return self._data[key]

    def __contains__(self, key):
        _check_var(key)
        return key in self._data

    def __len__(self):
        return len(self._data)

    def __iter__(self):
        return iter(self._data)

    def keys(self):
        """Return a list of all variables in the context object."""
        return self._data.keys()

    def values(self):
        """Return a list of all variables' values in the context object."""
        return self._data.values()

    def items(self):
        """Return a list of all variables and their values in the context object."""
        return self._data.items()

    __hash__ = None

    def __eq__(self, other):
        if not isinstance(other, Context):
            return NotImplemented
        return self._data == other._data


def _check_var(key):
    if not isinstance(key, ContextVar):
        raise TypeError('a ContextVar key was expected, got %r' % (key,))


class ContextVar:
    """A context variable."""

    def __init__(self, name, *, default=_MISSING):
        if not isinstance(name, str):
            raise TypeError('context variable name must be a str')
        self._name = name
        self._default = default

    @property
    def name(self):
        """The name of the variable."""
        return self._name

    def get(self, *args):
        """Return a value for the context variable for the current context."""
        if len(args) > 1:
            raise TypeError('get expected at most 1 argument, got %d' % len(args))
        value = _get_current()._data.get(self, _ABSENT)
        if value is not _ABSENT:
            return value
        if args:
            return args[0]
        if self._default is not _MISSING:
            return self._default
        raise LookupError(self)

    def set(self, value, /):
        """Call to set a new value for the context variable in the current context."""
        context = _get_current()
        token = Token(context, self, context._data.get(self, Token.MISSING))
        context._data = {**context._data, self: value}
        return token

    def reset(self, token, /):
        """Reset the context variable to the value it had before the set() that created the token."""
        if not isinstance(token, Token):
            raise TypeError('expected an instance of Token, got %r' % (token,))
        if token._used:
            raise RuntimeError('%r has already been used once' % (token,))
        if token._var is not self:
            raise ValueError('%r was created by a different ContextVar' % (token,))
        context = _get_current()
        if token._context is not context:
            raise ValueError('%r was created in a different Context' % (token,))
        data = dict(context._data)
        if token._old_value is Token.MISSING:
            data.pop(self, None)
        else:
            data[self] = token._old_value
        context._data = data
        token._used = True

    __class_getitem__ = classmethod(_generic_alias)

    def __repr__(self):
        r = '<ContextVar name=%r' % (self._name,)
        if self._default is not _MISSING:
            r += ' default=%r' % (self._default,)
        return r + ' at 0x%x>' % id(self)


class Token:
    """A token object returned by ContextVar.set()."""
    MISSING = _MISSING

    def __init__(self, context, var, old_value):
        self._context, self._var, self._old_value, self._used = context, var, old_value, False

    @property
    def var(self):
        """The ContextVar that created the token."""
        return self._var

    @property
    def old_value(self):
        """The value the variable had before the set() that created the token."""
        return self._old_value

    __class_getitem__ = classmethod(_generic_alias)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self._var.reset(self)

    def __repr__(self):
        return '<Token%s var=%r at 0x%x>' % (' used' if self._used else '', self._var, id(self))


def copy_context():
    """Return a copy of the current context."""
    return _get_current().copy()
