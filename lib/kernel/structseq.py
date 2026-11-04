"""Struct sequences (Objects/structseq.c): sys.flags, sys.version_info,
os.stat_result and the other named tuples that C code builds.

structseq_type(name, doc, fields, n_visible) makes the class: a tuple of the
first n_visible fields, with an attribute for each named field. The fields
after those are hidden: they are not items, and live in the instance dict.
A None name is an unnamed field. Instances of a type that is not
instantiable can only be made by structseq_make, as C code does.
"""

__all__ = ()

_dict = dict
_fields = {}                    # class -> its field names, for the C-API


def structseq_blank(cls):
    """PyStructSequence_New: an instance whose fields are all None."""
    fields = _fields[cls]
    n_visible = cls.n_sequence_fields
    return structseq_make(cls, (None,) * n_visible,
                          {f: None for f in fields[n_visible:] if f is not None})


def structseq_set_hidden(self, index, value):
    """PyStructSequence_SetItem of a field that is not an item."""
    self.__dict__[_fields[type(self)][index]] = value


def structseq_make(cls, visible, hidden=None):
    """An instance of a struct sequence type with the given values."""
    self = tuple.__new__(cls, visible)
    if hidden:
        self.__dict__.update(hidden)
    return self


def structseq_type(name, doc, fields, n_visible, instantiable=True):
    module, _, short = name.rpartition('.')
    n_fields = len(fields)
    names = tuple(f for f in fields if f is not None)

    def __new__(cls, /, *args, **kwargs):
        if not instantiable:
            raise TypeError("cannot create '" + name + "' instances")
        return _new(cls, *args, **kwargs)

    def _new(cls, sequence, dict=None):
        try:
            items = tuple(sequence)
        except TypeError:
            raise TypeError('constructor requires a sequence') from None
        if dict is not None and not isinstance(dict, _dict):
            raise TypeError(name + '() takes a dict as second arg, if any')
        n = len(items)
        exact = n_visible == n_fields
        if n < n_visible:
            raise TypeError('%s() takes a%s %d-sequence (%d-sequence given)' % (
                name, '' if exact else 'n at least', n_visible, n))
        if n > n_fields:
            raise TypeError('%s() takes a%s %d-sequence (%d-sequence given)' % (
                name, '' if exact else 'n at most', n_fields, n))
        hidden = {}
        for i in range(n_visible, n_fields):
            if fields[i] is not None:
                hidden[fields[i]] = items[i] if i < n else (
                    None if dict is None else dict.get(fields[i]))
        return structseq_make(cls, items[:n_visible], hidden)

    def __reduce__(self):
        hidden = {f: self.__dict__[f] for f in fields[n_visible:] if f is not None}
        return (type(self), (tuple(self), hidden))

    def __replace__(self, /, **changes):
        values = dict(zip(names, tuple(self) + tuple(self.__dict__[f] for f in fields[n_visible:]
                                                     if f is not None)))
        unknown = [k for k in changes if k not in values]
        if unknown:
            raise TypeError('Got unexpected field name(s): ' + repr(unknown))
        values.update(changes)
        everything = [values[f] if f is not None else None for f in fields]
        return structseq_make(type(self), everything[:n_visible],
                    {f: values[f] for f in fields[n_visible:] if f is not None})

    def __repr__(self):
        parts = ['%s=%r' % (names[i], self[i]) for i in range(len(self)) if i < len(names)]
        return name + '(' + ', '.join(parts) + ')'

    def __setattr__(self, key, value):
        if key in names:
            raise AttributeError('readonly attribute')
        raise AttributeError("'%s' object has no attribute '%s' and no __dict__ for setting "
                             "new attributes" % (name, key))

    def __delattr__(self, key):
        if key in names:
            raise AttributeError('readonly attribute')
        raise AttributeError("'%s' object has no attribute '%s'" % (name, key))

    def field(index, field_name):
        if index < n_visible:
            return property(lambda self: tuple.__getitem__(self, index))
        return property(lambda self: self.__dict__[field_name])

    ns = {
        '__doc__': doc, '__module__': module, '__new__': __new__, '__reduce__': __reduce__,
        '__replace__': __replace__, '__repr__': __repr__, '__setattr__': __setattr__,
        '__delattr__': __delattr__, 'n_sequence_fields': n_visible, 'n_fields': n_fields,
        'n_unnamed_fields': n_fields - len(names),
        '__match_args__': tuple(f for f in fields[:n_visible] if f is not None),
    }
    for index, field_name in enumerate(fields):
        if field_name is not None:
            ns[field_name] = field(index, field_name)
    cls = type(short, (tuple,), ns)
    _fields[cls] = fields
    return cls


