"""The exception classes (Objects/exceptions.c) and the helpers the
interpreter calls for raise, with and except* (Python/ceval.c do_raise,
_PyEval_ExceptionGroupMatch, intrinsics.c, _PyObject_LookupSpecialMethod).

The layouts and the attributes that read them are native
(runtime/interp/exceptions.c): args, __traceback__, __context__, __cause__,
errno, name, obj, msg, code, value, ... This module is their behaviour:
construction, str and repr, the pickling hooks, OSError's errno mapping and
exception groups. Types without behaviour of their own need nothing here.
"""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, alloc, kernel_set,
    kernel_get, ns_get, ns_set, ns_items, instance_dict, is_subtype, exc_set_word, exc_word,
    handled_exception, set_handled_exception, is_exception, is_exception_class, list_append,
    caller_traceback, is_callable, int_text, int_format, str_repeat, str_len, str_item, str_ord,
    bytes_ord,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type
call_special = _boot.call_special
generic_getattr = kernel_get('generic_getattr')
setattr_ = kernel_get('setattr')

__all__ = ('ExceptionGroup',)


def _first(seq):
    for x in seq:
        return x


def _text(x):
    """str(x) for the messages."""
    if type(x) is str:
        return x
    return call_special(type_lookup(type(x), '__str__'), x)


def _repr(x):
    return call_special(type_lookup(type(x), '__repr__'), x)


def _type_name(obj):
    return type_tp_name(type(obj))


def _no_keywords(cls, kwds):
    """The refusal of keywords; cls's name is made only for the message."""
    if kwds:
        error(TypeError, str_join('', (type_tp_name(cls), '() takes no keyword arguments')))


def _store_keywords(self, cls, kwds, names):
    """Stores the keyword arguments of cls's __init__ in the words names lists in order."""
    for key, value in ns_items(kwds):
        word = -1
        i = 0
        for name in names:
            if key == name:
                word = i
            i = i + 1
        if word < 0:
            error(TypeError, str_join('', (
                "'", key, "' is an invalid keyword argument for ", cls, '()')))
        _set(self, word, value)


# The words of a native exception layout (runtime/interp/exceptions.c), read and
# written as CPython's C code reads and writes its struct fields: a subclass may
# shadow one with a property, which neither its __init__ nor its __str__ sees.
_set = exc_set_word
_word = exc_word


def _init(self, args, kwds):
    """BaseException_init."""
    _no_keywords(type(self), kwds)
    self.args = args


# BaseException

@kernel_type(BaseException)
class BaseException:
    """Common base class for all exceptions"""

    def __new__(cls, /, *args, **kwds):
        self = alloc(cls, BaseException)
        self.args = args
        return self

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)

    def __str__(self):
        args = self.args
        n = size(args)
        if n == 0:
            return ''
        if n == 1:
            return _text(_first(args))
        return _text(args)

    def __repr__(self):
        args = self.args
        name = type(self).__name__
        n = size(args)
        if n == 0:
            return str_join('', (name, '()'))
        if n == 1:
            return str_join('', (name, '(', _repr(_first(args)), ')'))
        return str_join('', (name, _repr(args)))

    def __reduce__(self):
        d = instance_dict(self, False)
        if d:
            return (type(self), self.args, d)
        return (type(self), self.args)

    def __setstate__(self, state):
        if state is not None:
            if not kind(state, 'dict'):
                error(TypeError, 'state is not a dictionary')
            for key, value in ns_items(state):
                setattr_(self, key, value)
        return None

    def with_traceback(self, tb, /):
        """Set self.__traceback__ to tb and return self."""
        self.__traceback__ = tb
        return self

    def add_note(self, note, /):
        """Add a note to the exception"""
        if not kind(note, 'str'):
            error(TypeError, str_join('', (
                "note must be a str, not '", _type_name(note), "'")))
        notes = generic_getattr(self, '__notes__', True)
        if notes is MISSING:
            notes = []
            setattr_(self, '__notes__', notes)
        elif not kind(notes, 'list'):
            error(TypeError, 'Cannot add note: __notes__ is not a list')
        list_append(notes, note)
        return None


_base_dict_new = type_lookup(BaseException, '__new__')


@kernel_type(SystemExit)
class SystemExit:
    """Request to exit from the interpreter."""

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)
        n = size(args)
        if n == 1:
            _set(self, 0, _first(args))
        elif n > 1:
            _set(self, 0, args)


@kernel_type(StopIteration)
class StopIteration:
    """Signal the end from iterator.__next__()."""

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)
        _set(self, 0, _first(args) if args else None)


@kernel_type(KeyError)
class KeyError:
    """Mapping key not found."""

    def __str__(self):
        args = self.args
        if size(args) == 1:
            return _repr(_first(args))
        return _base_str(self)


_base_str = type_lookup(BaseException, '__str__')


@kernel_type(NameError)
class NameError:
    """Name not found globally."""

    def __init__(self, /, *args, **kwds):
        self.args = args
        _store_keywords(self, 'NameError', kwds, ('name',))


@kernel_type(AttributeError)
class AttributeError:
    """Attribute not found."""

    def __init__(self, /, *args, **kwds):
        self.args = args
        _store_keywords(self, 'AttributeError', kwds, ('obj', 'name'))

    def __reduce__(self):
        state = {}
        d = instance_dict(self, False)
        if d:
            for key, value in ns_items(d):
                ns_set(state, key, value)
        if _word(self, 1) is not None:
            ns_set(state, 'name', _word(self, 1))
        ns_set(state, 'args', self.args)
        return (type(self), self.args, state)


@kernel_type(ImportError)
class ImportError:
    """Import can't find module, or can't find name in module."""

    def __init__(self, /, *args, **kwds):
        self.args = args
        for key, value in ns_items(kwds):
            if key != 'name' and key != 'path' and key != 'name_from':
                error(TypeError, str_join('', (
                    "ImportError() got an unexpected keyword argument '", key, "'")))
        _set(self, 1, ns_get(kwds, 'name', None))
        _set(self, 2, ns_get(kwds, 'path', None))
        _set(self, 3, ns_get(kwds, 'name_from', None))
        _set(self, 0, _first(args) if size(args) == 1 else None)

    def __str__(self):
        msg = _word(self, 0)
        if kind(msg, 'str') and type(msg) is str:
            return msg
        return _base_str(self)

    def __reduce__(self):
        state = {}
        d = instance_dict(self, False)
        if d:
            for key, value in ns_items(d):
                ns_set(state, key, value)
        for i, key in ((1, 'name'), (2, 'path'), (3, 'name_from')):
            if _word(self, i) is not None:
                ns_set(state, key, _word(self, i))
        if state:
            return (type(self), self.args, state)
        return (type(self), self.args)


# OSError

@kernel_type(OSError)
class OSError:
    """Base class for I/O related errors."""

    def __new__(cls, /, *args, **kwds):
        if _use_init(cls):
            self = alloc(cls, OSError)
            self.args = ()
            return self
        _no_keywords(cls, kwds)
        errno, strerror, filename, filename2 = _oserror_args(args)
        if cls is OSError and errno is not MISSING and kind_int(errno):
            mapped = _errno_class(errno)
            if mapped is not None:
                cls = mapped
        self = alloc(cls, OSError)
        _oserror_fill(self, args, errno, strerror, filename, filename2)
        return self

    def __init__(self, /, *args, **kwds):
        if not _use_init(type(self)):
            return None
        _no_keywords(type(self), kwds)
        errno, strerror, filename, filename2 = _oserror_args(args)
        _oserror_fill(self, args, errno, strerror, filename, filename2)

    def __str__(self):
        errno = _word(self, 0)
        strerror = _word(self, 1)
        filename = _word(self, 2)
        if filename is not None:
            if _word(self, 3) is not None:
                return str_join('', ('[Errno ', _text(errno), '] ', _text(strerror), ': ',
                                     _repr(filename), ' -> ', _repr(_word(self, 3))))
            return str_join('', ('[Errno ', _text(errno), '] ', _text(strerror), ': ',
                                 _repr(filename)))
        if errno is not None and strerror is not None:
            return str_join('', ('[Errno ', _text(errno), '] ', _text(strerror)))
        return _base_str(self)

    def __reduce__(self):
        args = self.args
        if size(args) == 2 and _word(self, 2) is not None:
            a, b = args
            if _word(self, 3) is not None:
                args = (a, b, _word(self, 2), None, _word(self, 3))
            else:
                args = (a, b, _word(self, 2))
        d = instance_dict(self, False)
        if d:
            return (type(self), args, d)
        return (type(self), args)


_oserror_new = type_lookup(OSError, '__new__')
_oserror_init = type_lookup(OSError, '__init__')


def kind_int(x):
    return isinstance(x, int)


def _use_init(cls):
    """oserror_use_init: a subclass that defines __init__ but not __new__ gets
    its arguments there instead."""
    return type_lookup(cls, '__init__') is not _oserror_init \
        and type_lookup(cls, '__new__') is _oserror_new


def _oserror_args(args):
    """oserror_parse_args: errno, strerror, filename, filename2 of 2 to 5
    arguments (a winerror in fourth place is ignored); MISSING when not
    given."""
    n = size(args)
    if n == 2:
        errno, strerror = args
        return errno, strerror, MISSING, MISSING
    if n == 3:
        errno, strerror, filename = args
        return errno, strerror, filename, MISSING
    if n == 4:
        errno, strerror, filename, winerror = args
        return errno, strerror, filename, MISSING
    if n == 5:
        errno, strerror, filename, winerror, filename2 = args
        return errno, strerror, filename, filename2
    return MISSING, MISSING, MISSING, MISSING


def _oserror_fill(self, args, errno, strerror, filename, filename2):
    """oserror_init."""
    if filename is not MISSING and filename is not None:
        if type(self) is BlockingIOError and isinstance(filename, int):
            self.characters_written = filename
        else:
            _set(self, 2, filename)
            if filename2 is not MISSING and filename2 is not None:
                _set(self, 3, filename2)
            if errno is not MISSING:
                a, b = _two(args)
                args = (a, b)
    if errno is not MISSING:
        _set(self, 0, errno)
        _set(self, 1, strerror)
    self.args = args


def _two(args):
    """The first two items of args."""
    n = size(args)
    if n == 2:
        a, b = args
    elif n == 3:
        a, b, c = args
    elif n == 4:
        a, b, c, d = args
    else:
        a, b, c, d, e = args
    return a, b


# errno values of Linux, and the OSError subclass each one raises.
_ERRNO_CLASSES = (
    (1, PermissionError), (2, FileNotFoundError), (3, ProcessLookupError),
    (4, InterruptedError), (10, ChildProcessError), (11, BlockingIOError),
    (13, PermissionError), (17, FileExistsError), (20, NotADirectoryError),
    (21, IsADirectoryError), (32, BrokenPipeError), (103, ConnectionAbortedError),
    (104, ConnectionResetError), (108, BrokenPipeError), (110, TimeoutError),
    (111, ConnectionRefusedError), (114, BlockingIOError), (115, BlockingIOError),
)


def _errno_class(errno):
    for number, cls in _ERRNO_CLASSES:
        if number == errno:
            return cls
    return None


# SyntaxError

@kernel_type(SyntaxError)
class SyntaxError:
    """Invalid syntax."""

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)
        n = size(args)
        if n >= 1:
            _set(self, 0, _first(args))
        if n == 2:
            info = _items(_second(args))
            count = size(info)
            if count < 4:
                error(TypeError, str_join('', (
                    'function takes at least 4 arguments (', int_text(count), ' given)')))
            if count > 7:
                error(TypeError, str_join('', (
                    'function takes at most 7 arguments (', int_text(count), ' given)')))
            filename, lineno, offset, text, end_lineno, end_offset, metadata = _pad(info, 7)
            _set(self, 1, filename)
            _set(self, 2, lineno)
            _set(self, 3, offset)
            _set(self, 4, text)
            if end_lineno is not MISSING:
                _set(self, 5, end_lineno)
            if end_offset is not MISSING:
                _set(self, 6, end_offset)
            if metadata is not MISSING:
                _set(self, 8, metadata)
            if end_lineno is not MISSING and end_offset is MISSING:
                error(TypeError, 'end_offset must be provided when end_lineno is provided')

    def __str__(self):
        filename = _word(self, 1)
        lineno = _word(self, 2)
        if kind(filename, 'str'):
            filename = _basename(filename)
        else:
            filename = None
        have_lineno = lineno is not None and isinstance(lineno, int)
        msg = _word(self, 0)
        if filename is None and not have_lineno:
            return _text(msg)
        if filename is not None and have_lineno:
            return str_join('', (_text(msg), ' (', filename, ', line ', _text(lineno), ')'))
        if filename is not None:
            return str_join('', (_text(msg), ' (', filename, ')'))
        return str_join('', (_text(msg), ' (line ', _text(lineno), ')'))


def _second(seq):
    first = True
    for x in seq:
        if not first:
            return x
        first = False


def _items(seq):
    return (*seq,)


def _pad(items, n):
    """The n first items, MISSING for each one beyond them."""
    result = [*items]
    while size(result) < n:
        result = [*result, MISSING]
    return (*result,)


def _basename(name):
    """The text after the last '/'."""
    base = ''
    for part in _split(name, '/'):
        base = part
    return base


def _split(text, sep):
    parts = []
    current = []
    for ch in text:
        if ch == sep:
            parts = [*parts, str_join('', (*current,))]
            current = []
        else:
            current = [*current, ch]
    return [*parts, str_join('', (*current,))]


# UnicodeError

def _unicode_args(self, args, shapes, what):
    """Checks args against shapes: 's' a str, 'o' any object, 'n' an int."""
    n = size(shapes)
    if size(args) != n:
        error(TypeError, str_join('', (
            what, ' takes exactly ', _text(n), ' arguments (', int_text(size(args)), ' given)')))
    index = 0
    for arg in args:
        index = index + 1
        shape = _nth(shapes, index)
        if (shape == 's' and not kind(arg, 'str')) or (shape == 'n' and not isinstance(arg, int)):
            error(TypeError, str_join('', (
                what, '() argument ', _text(index), ' must be ',
                'str' if shape == 's' else 'int', ', not ', _type_name(arg))))


def _nth(items, n):
    """The nth item of a str or tuple, counting from 1."""
    i = 0
    for x in items:
        i = i + 1
        if i == n:
            return x


@kernel_type(UnicodeEncodeError)
class UnicodeEncodeError:
    """Unicode encoding error."""

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)
        _unicode_args(self, args, 'ssnns', 'UnicodeEncodeError')
        encoding, obj, self.start, self.end, reason = args
        _set(self, 0, encoding)
        _set(self, 1, obj)
        _set(self, 4, reason)

    def __str__(self):
        return _unicode_str(self, 'encode', False)


@kernel_type(UnicodeDecodeError)
class UnicodeDecodeError:
    """Unicode decoding error."""

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)
        _unicode_args(self, args, 'sbnns', 'UnicodeDecodeError')
        encoding, obj, self.start, self.end, reason = args
        _set(self, 0, encoding)
        _set(self, 1, obj if kind(obj, 'bytes') else bytes(obj))
        _set(self, 4, reason)

    def __str__(self):
        return _unicode_str(self, 'decode', True)


@kernel_type(UnicodeTranslateError)
class UnicodeTranslateError:
    """Unicode translation error."""

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)
        _unicode_args(self, args, 'snns', 'UnicodeTranslateError')
        obj, self.start, self.end, reason = args
        _set(self, 1, obj)
        _set(self, 4, reason)
        _set(self, 0, None)

    def __str__(self):
        if _word(self, 1) is None:
            return ''
        reason = _text(_word(self, 4))
        obj = _error_object(self, False)
        start = self.start
        end = self.end
        if start >= 0 and start < _length(obj) and end == start + 1:
            return str_join('', ("can't translate character '", _escape(obj, start),
                                 "' in position ", _text(start), ': ', reason))
        return str_join('', ("can't translate characters in position ", _text(start), '-',
                             _text(end - 1), ': ', reason))


def _length(seq):
    if kind(seq, 'str') or kind(seq, 'bytes'):
        return size(seq)
    n = 0
    for x in seq:
        n = n + 1
    return n


def _hex(value, digits):
    text = int_format(value, 16, False)
    return str_join('', (str_repeat('0', digits - str_len(text)), text))


def _escape(obj, index):
    """The character of a str at index, as \\xNN, \\uNNNN or \\UNNNNNNNN."""
    code = str_ord(obj, index)
    if code <= 0xff:
        return str_join('', ('\\x', _hex(code, 2)))
    if code <= 0xffff:
        return str_join('', ('\\u', _hex(code, 4)))
    return str_join('', ('\\U', _hex(code, 8)))


def _error_object(self, decoding):
    """The object of a Unicode error, which str() reads after the side effects
    of its other attributes' __str__: it must still be there, and the right type."""
    obj = _word(self, 1)
    if obj is None:
        error(TypeError, "UnicodeError 'object' attribute is not set")
    if not kind(obj, 'bytes' if decoding else 'str'):
        error(TypeError, str_join('', ("UnicodeError 'object' attribute must be a ",
                                       'bytes' if decoding else 'string')))
    return obj


def _unicode_str(self, verb, decoding):
    if _word(self, 1) is None:
        return ''
    reason = _text(_word(self, 4))
    encoding = _text(_word(self, 0))
    obj = _error_object(self, decoding)
    start = self.start
    end = self.end
    if start >= 0 and start < _length(obj) and end <= _length(obj) and end == start + 1:
        if decoding:
            what = str_join('', ('byte 0x', _hex(_nth_byte(obj, start), 2)))
        else:
            what = str_join('', ("character '", _escape(obj, start), "'"))
        return str_join('', ("'", encoding, "' codec can't ", verb, ' ', what,
                             ' in position ', _text(start), ': ', reason))
    return str_join('', ("'", encoding, "' codec can't ", verb, ' bytes' if decoding else ' characters',
                         ' in position ', _text(start), '-', _text(end - 1), ': ', reason))


def _nth_byte(data, index):
    return bytes_ord(data, index)


# Exception groups

@kernel_type(BaseExceptionGroup)
class BaseExceptionGroup:
    """A combination of multiple unrelated exceptions."""

    def __new__(cls, /, *args, **kwds):
        if size(args) != 2:
            error(TypeError, str_join('', (
                'BaseExceptionGroup.__new__() takes exactly 2 arguments (',
                int_text(size(args)), ' given)')))
        message, exceptions = args
        if not kind(message, 'str'):
            error(TypeError, str_join('', (
                'BaseExceptionGroup.__new__() argument 1 must be str, not ', _type_name(message))))
        if not _is_sequence(exceptions):
            error(TypeError, 'second argument (exceptions) must be a sequence')
        excs_str = None
        if not kind(exceptions, 'list') and not kind(exceptions, 'tuple'):
            excs_str = _repr(exceptions)
        exceptions = (*exceptions,)
        if not exceptions:
            error(ValueError, 'second argument (exceptions) must be a non-empty sequence')
        nested_base = False
        seen = []
        for exc in exceptions:
            if not is_exception(exc):
                error(ValueError, str_join('', (
                    'Item ', int_text(size(seen)), ' of second argument (exceptions) '
                    'is not an exception')))
            if not isinstance(exc, Exception):
                nested_base = True
            seen = [*seen, exc]
        if cls is ExceptionGroup:
            if nested_base:
                error(TypeError, 'Cannot nest BaseExceptions in an ExceptionGroup')
        elif cls is BaseExceptionGroup:
            if not nested_base:
                cls = ExceptionGroup
        elif nested_base and issubclass(cls, Exception):
            error(TypeError, str_join('', (
                "Cannot nest BaseExceptions in '", type_tp_name(cls), "'")))
        self = alloc(cls, BaseExceptionGroup)
        self.args = args
        exc_set_word(self, 0, message)
        exc_set_word(self, 1, exceptions)
        exc_set_word(self, 2, excs_str)
        return self

    def __init__(self, /, *args, **kwds):
        _init(self, args, kwds)

    def __str__(self):
        n = size(self.exceptions)
        return str_join('', (self.message, ' (', int_text(n), ' sub-exception',
                             's' if n > 1 else '', ')'))

    def __repr__(self):
        excs_str = exc_word(self, 2)
        if excs_str is None:
            args = self.args
            if size(args) == 2 and kind(_second(args), 'list'):
                excs_str = _repr([*self.exceptions])
            else:
                excs_str = _repr(self.exceptions)
        return str_join('', (type(self).__name__, '(', _repr(self.message), ', ', excs_str, ')'))

    def derive(self, excs, /):
        return BaseExceptionGroup(self.message, excs)

    def split(self, matcher, /):
        """Split the group by type, tuple of types or predicate: (match, rest)."""
        return _split_recursive(self, _matcher_of(matcher), matcher, True)

    def subgroup(self, matcher, /):
        """The part of the group that matches, or None."""
        match, rest = _split_recursive(self, _matcher_of(matcher), matcher, False)
        return match


_BY_TYPE, _BY_PREDICATE, _BY_LEAVES = 0, 1, 2


def _is_sequence(x):
    if kind(x, 'list') or kind(x, 'tuple') or kind(x, 'str'):
        return True
    return type_lookup(type(x), '__getitem__') is not MISSING and not kind(x, 'dict')


def _matcher_of(value):
    if is_callable(value) and not kind(value, 'type'):
        return _BY_PREDICATE
    if is_exception_class(value):
        return _BY_TYPE
    if kind(value, 'tuple'):
        ok = True
        for cls in value:
            if not is_exception_class(cls):
                ok = False
        if ok:
            return _BY_TYPE
    error(TypeError, 'expected an exception type, a tuple of exception types, '
                     'or a callable (other than a class)')


def _is_group(exc):
    return is_subtype(type(exc), BaseExceptionGroup)


def _matches_type(exc, matcher):
    if kind(matcher, 'tuple'):
        for cls in matcher:
            if is_subtype(type(exc), cls):
                return True
        return False
    return is_subtype(type(exc), matcher)


def _check_match(exc, how, matcher):
    if how == _BY_TYPE:
        return _matches_type(exc, matcher)
    if how == _BY_PREDICATE:
        return True if matcher(exc) else False
    if _is_group(exc):
        return False
    for leaf in matcher:
        if leaf is exc:
            return True
    return False


def _split_recursive(exc, how, matcher, construct_rest):
    """exceptiongroup_split_recursive: (match, rest), None for an empty part."""
    if _check_match(exc, how, matcher):
        return (exc, None)
    if not _is_group(exc):
        return (None, exc if construct_rest else None)
    matches = []
    rests = []
    for e in exc.exceptions:
        m, r = _split_recursive(e, how, matcher, construct_rest)
        if m is not None:
            matches = [*matches, m]
        if r is not None:
            rests = [*rests, r]
    return (_subset(exc, matches), _subset(exc, rests) if construct_rest else None)


def _subset(orig, excs):
    """exceptiongroup_subset: a derived group with orig's metadata, None if empty."""
    if not excs:
        return None
    eg = orig.derive(excs)
    if not _is_group(eg):
        error(TypeError, 'derive must return an instance of BaseExceptionGroup')
    tb = orig.__traceback__
    if tb is not None:
        eg.__traceback__ = tb
    eg.__context__ = orig.__context__
    eg.__cause__ = orig.__cause__
    notes = generic_getattr(orig, '__notes__', True)
    if notes is not MISSING and _is_sequence(notes):
        setattr_(eg, '__notes__', [*notes])
    return eg


def _leaves(exc, found):
    if exc is None:
        return found
    if not _is_group(exc):
        return [*found, exc]
    for e in exc.exceptions:
        found = _leaves(e, found)
    return found


def _projection(eg, keep):
    """exception_group_projection: the part of eg made of the leaves of the
    groups in keep."""
    leaves = []
    for group in keep:
        leaves = _leaves(group, leaves)
    match, rest = _split_recursive(eg, _BY_LEAVES, leaves, False)
    return match


def _same_metadata(a, b):
    return a.__traceback__ is b.__traceback__ and a.__cause__ is b.__cause__ \
        and a.__context__ is b.__context__


def prep_reraise_star(orig, excs):
    """_PyExc_PrepReraiseStar: the exception to raise after the except*
    clauses ran; excs holds what each raised or reraised."""
    if not excs:
        return None
    if not _is_group(orig):
        return _first(excs)
    raised = []
    reraised = []
    for e in excs:
        if e is None:
            continue
        if _same_metadata(e, orig):
            reraised = [*reraised, e]
        else:
            raised = [*raised, e]
    reraised_eg = _projection(orig, reraised)
    if not raised:
        return reraised_eg
    if reraised_eg is not None:
        raised = [*raised, reraised_eg]
    if size(raised) > 1:
        return BaseExceptionGroup('', raised)
    return _first(raised)


def exc_group_match(exc_value, match_type):
    """_PyEval_ExceptionGroupMatch for CHECK_EG_MATCH: (rest, match) of the
    exception being handled by an except* clause, which becomes the handled
    exception when it matches."""
    _check_except_star(match_type)
    match = None
    rest = None
    if exc_value is None:
        pass
    elif _matches_type(exc_value, match_type):
        if _is_group(exc_value):
            match = exc_value
        else:
            match = ExceptionGroup('', (exc_value,))
            match.__traceback__ = caller_traceback()
    elif _is_group(exc_value):
        pair = exc_value.split(match_type)
        if not kind(pair, 'tuple'):
            error(TypeError, str_join('', (
                _type_name(exc_value), '.split must return a tuple, not ', _type_name(pair))))
        if size(pair) < 2:
            error(TypeError, str_join('', (
                _type_name(exc_value), '.split must return a 2-tuple, got tuple of size ',
                int_text(size(pair)))))
        match = _first(pair)
        rest = _second(pair)
    else:
        rest = exc_value
    if match is not None:
        set_handled_exception(match)
    return (rest, match)


def _check_except_star(match_type):
    """_PyEval_CheckExceptStarTypeValid."""
    classes = match_type if kind(match_type, 'tuple') else (match_type,)
    for cls in classes:
        if not is_exception_class(cls):
            error(TypeError, 'catching classes that do not inherit from BaseException '
                             'is not allowed')
    for cls in classes:
        if is_subtype(cls, BaseExceptionGroup):
            error(TypeError, 'catching ExceptionGroup with except* is not allowed. '
                             'Use except instead.')


ExceptionGroup = type('ExceptionGroup', (BaseExceptionGroup, Exception),
                      {'__module__': 'builtins', '__doc__': 'Exception group of Exceptions'})


# The interpreter's entry points

def do_raise(exc, cause):
    """RAISE_VARARGS: raise exc, from cause unless that is MISSING."""
    if is_exception_class(exc):
        value = exc()
        if not is_exception(value):
            error(TypeError, str_join('', (
                'calling ', _repr(exc), ' should have returned an instance of BaseException, not ',
                _repr(type(value)))))
    elif is_exception(exc):
        value = exc
    else:
        error(TypeError, 'exceptions must derive from BaseException')
    if cause is not MISSING:
        if is_exception_class(cause):
            fixed = cause()
            if not is_exception(fixed):
                error(TypeError, str_join('', (
                    'calling ', _repr(cause), ' should have returned an instance of '
                    'BaseException, not ', _repr(type(fixed)))))
        elif is_exception(cause):
            fixed = cause
        elif cause is None:
            fixed = None
        else:
            error(TypeError, 'exception causes must derive from BaseException')
        value.__cause__ = fixed
    error(type(value), value)


def _special_name(which):
    """The names of the SPECIAL_* constants of pycore_opcode_utils.h."""
    if which == 0:
        return '__enter__'
    if which == 1:
        return '__exit__'
    if which == 2:
        return '__aenter__'
    return '__aexit__'


def load_special(owner, which):
    """LOAD_SPECIAL: the special method called `which` bound to owner, looked
    up on its type."""
    tp = type(owner)
    method = type_lookup(tp, _special_name(which))
    if method is MISSING:
        error(TypeError, _special_error(tp, which))
    get = type_lookup(type(method), '__get__')
    if get is MISSING:
        return method
    return call_special(get, method, owner, tp)


def _special_error(tp, which):
    """The error of a missing special method, suggesting the async form when
    the type has the other pair."""
    sync = which < 2
    suggest = type_lookup(tp, '__aenter__' if sync else '__enter__') is not MISSING \
        and type_lookup(tp, '__aexit__' if sync else '__exit__') is not MISSING
    if suggest:
        suffix = (" but it supports the asynchronous context manager protocol. "
                  "Did you mean to use 'async with'?" if sync else
                  " but it supports the context manager protocol. Did you mean to use 'with'?")
    else:
        suffix = ''
    return str_join('', (
        "'", type_tp_name(tp), "' object does not support the ",
        '' if sync else 'asynchronous ', 'context manager protocol (missed ',
        _special_name(which), ' method)', suffix))


kernel_set('do_raise', do_raise)
kernel_set('load_special', load_special)
kernel_set('exc_group_match', exc_group_match)
kernel_set('prep_reraise_star', prep_reraise_star)


# sys.exception and sys.exc_info (sys.py installs them)

def exception():
    """Return the current exception.

Return the most recent exception caught by an except clause in the calling
stack or in an older stack frame, or None if no such exception exists."""
    return handled_exception()


def exc_info():
    """Return current exception information: (type, value, traceback)."""
    e = handled_exception()
    if e is None:
        return (None, None, None)
    return (type(e), e, e.__traceback__)


