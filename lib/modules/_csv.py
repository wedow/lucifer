"""CSV parsing and writing.
"""

# _csv (Modules/_csv.c) as Python: the Dialect type with its validation,
# the reader's character state machine and the writer's quoting rules.
# A character option that is not set (NOT_SET in C) is None here.

from _lucifer import type_tp_name as _tp_name

QUOTE_MINIMAL, QUOTE_ALL, QUOTE_NONNUMERIC, QUOTE_NONE, QUOTE_STRINGS, QUOTE_NOTNULL = range(6)

_INT_MAX = 2147483647
_SSIZE_MAX = 9223372036854775807
_MISSING = object()
_EOL = object()   # end of an input line, fed to the reader after its last character

_dialects = {}
_field_limit = 128 * 1024


class Error(Exception):
    __module__ = '_csv'


def _type_name(ob):
    """The fully qualified name of ob's type, as %T prints it."""
    tp = type(ob)
    module = tp.__module__
    if module in ('builtins', '__main__'):
        return tp.__qualname__
    return '%s.%s' % (module, tp.__qualname__)


def _no_instances(cls):
    raise TypeError("cannot create '%s.%s' instances" % (cls.__module__, cls.__name__))


# ---------------------------------------------------------------------------
# Dialect.

def _get_dialect_from_registry(name):
    try:
        return _dialects[name]
    except KeyError:
        raise Error('unknown dialect') from None


def _set_char(name, src, dflt, allow_none):
    if src is _MISSING:
        return dflt
    if src is None and allow_none:
        return None
    what = 'a unicode character or None' if allow_none else 'a unicode character'
    if not isinstance(src, str):
        raise TypeError('"%s" must be %s, not %s' % (name, what, _type_name(src)))
    if len(src) != 1:
        raise TypeError('"%s" must be %s, not a string of length %d' % (name, what, len(src)))
    return src


def _set_int(name, src, dflt):
    if src is _MISSING:
        return dflt
    if type(src) is not int:
        raise TypeError('"%s" must be an integer, not %s' % (name, _type_name(src)))
    if src > _INT_MAX or src < -_INT_MAX - 1:
        raise OverflowError('Python int too large to convert to C int')
    return src


def _set_str(name, src, dflt):
    if src is _MISSING:
        return dflt
    if not isinstance(src, str):
        raise TypeError('"%s" must be a string, not %s' % (name, _type_name(src)))
    return src


def _check_char(name, c, lineterminator, allowspace):
    if c == '\r' or c == '\n' or (c == ' ' and not allowspace):
        raise ValueError('bad %s value' % name)
    if c is not None and c in lineterminator:
        raise ValueError('bad %s or lineterminator value' % name)


def _check_chars(name1, name2, c1, c2):
    if c1 == c2 and c1 is not None:
        raise ValueError('bad %s or %s value' % (name1, name2))


_DIALECT_ATTRS = ('delimiter', 'doublequote', 'escapechar', 'lineterminator', 'quotechar', 'quoting',
                  'skipinitialspace', 'strict')


class Dialect:
    """CSV dialect

The Dialect type records CSV parsing and generation options.
"""

    __module__ = '_csv'
    __slots__ = ('_delimiter', '_doublequote', '_escapechar', '_lineterminator', '_quotechar', '_quoting',
                 '_skipinitialspace', '_strict')

    def __new__(cls, dialect=_MISSING, delimiter=_MISSING, doublequote=_MISSING, escapechar=_MISSING,
                lineterminator=_MISSING, quotechar=_MISSING, quoting=_MISSING, skipinitialspace=_MISSING,
                strict=_MISSING):
        values = [delimiter, doublequote, escapechar, lineterminator, quotechar, quoting, skipinitialspace,
                  strict]
        if dialect is not _MISSING:
            if isinstance(dialect, str):
                dialect = _get_dialect_from_registry(dialect)
            # Can we reuse this instance?
            if isinstance(dialect, Dialect) and all(v is _MISSING for v in values):
                return dialect
            for i, name in enumerate(_DIALECT_ATTRS):
                if values[i] is _MISSING:
                    try:
                        values[i] = getattr(dialect, name)
                    except Exception:
                        pass
        delimiter, doublequote, escapechar, lineterminator, quotechar, quoting, skipinitialspace, strict = values

        self = object.__new__(cls)
        self._delimiter = _set_char('delimiter', delimiter, ',', False)
        self._doublequote = True if doublequote is _MISSING else bool(doublequote)
        self._escapechar = _set_char('escapechar', escapechar, None, True)
        self._lineterminator = _set_str('lineterminator', lineterminator, '\r\n')
        self._quotechar = _set_char('quotechar', quotechar, '"', True)
        self._quoting = _set_int('quoting', quoting, QUOTE_MINIMAL)
        self._skipinitialspace = False if skipinitialspace is _MISSING else bool(skipinitialspace)
        self._strict = False if strict is _MISSING else bool(strict)

        if not 0 <= self._quoting <= QUOTE_NOTNULL:
            raise TypeError('bad "quoting" value')
        if quotechar is None and quoting is _MISSING:
            self._quoting = QUOTE_NONE
        if self._quoting != QUOTE_NONE and self._quotechar is None:
            raise TypeError('quotechar must be set if quoting enabled')
        term = self._lineterminator
        _check_char('delimiter', self._delimiter, term, True)
        _check_char('escapechar', self._escapechar, term, not self._skipinitialspace)
        _check_char('quotechar', self._quotechar, term, not self._skipinitialspace)
        _check_chars('delimiter', 'escapechar', self._delimiter, self._escapechar)
        _check_chars('delimiter', 'quotechar', self._delimiter, self._quotechar)
        _check_chars('escapechar', 'quotechar', self._escapechar, self._quotechar)
        return self

    delimiter = property(lambda self: self._delimiter)
    doublequote = property(lambda self: self._doublequote)
    escapechar = property(lambda self: self._escapechar)
    lineterminator = property(lambda self: self._lineterminator)
    quotechar = property(lambda self: self._quotechar)
    quoting = property(lambda self: self._quoting)
    skipinitialspace = property(lambda self: self._skipinitialspace)
    strict = property(lambda self: self._strict)

    def __reduce__(self, *args):
        """raises an exception to avoid pickling"""
        raise TypeError("cannot pickle '%.100s' instances" % type(self).__name__)

    __reduce_ex__ = __reduce__


def _call_dialect(dialect, kwargs):
    if dialect is _MISSING:
        return Dialect(**kwargs)
    return Dialect(dialect, **kwargs)


# ---------------------------------------------------------------------------
# Reader.

(_START_RECORD, _START_FIELD, _ESCAPED_CHAR, _IN_FIELD, _IN_QUOTED_FIELD, _ESCAPE_IN_QUOTED_FIELD,
 _QUOTE_IN_QUOTED_FIELD, _EAT_CRNL, _AFTER_ESCAPED_CRNL) = range(9)


class Reader:
    """CSV reader

Reader objects are responsible for reading and parsing tabular data
in CSV format.
"""

    __module__ = '_csv'
    __slots__ = ('_input_iter', '_dialect', '_fields', '_state', '_field', '_unquoted_field', '_line_num')

    def __new__(cls, *args, **kwargs):
        _no_instances(cls)

    dialect = property(lambda self: self._dialect)
    line_num = property(lambda self: self._line_num)

    def __iter__(self):
        return self

    def __next__(self):
        self._parse_reset()
        while True:
            line = next(self._input_iter, _MISSING)
            if line is _MISSING:
                if self._field or self._state == _IN_QUOTED_FIELD:
                    if self._dialect._strict:
                        raise Error('unexpected end of data')
                    self._save_field()
                    break
                raise StopIteration
            if not isinstance(line, str):
                raise Error('iterator should return strings, not %.200s '
                            '(the file should be opened in text mode)' % _tp_name(type(line)))
            if self._fields is None:
                raise Error('iterator has already advanced the reader')
            self._line_num += 1
            for c in line:
                self._process_char(c)
            self._process_char(_EOL)
            if self._state == _START_RECORD:
                break
        fields = self._fields
        self._fields = None
        return fields

    def _parse_reset(self):
        self._fields = []
        self._field = []
        self._state = _START_RECORD
        self._unquoted_field = False

    def _save_field(self):
        quoting = self._dialect._quoting
        if self._unquoted_field and not self._field and (quoting == QUOTE_NOTNULL or quoting == QUOTE_STRINGS):
            field = None
        else:
            field = ''.join(self._field)
            if self._unquoted_field and field and (quoting == QUOTE_NONNUMERIC or quoting == QUOTE_STRINGS):
                field = float(field)
            self._field = []
        self._fields.append(field)

    def _add_char(self, c):
        if len(self._field) >= _field_limit:
            raise Error('field larger than field limit (%d)' % _field_limit)
        self._field.append(c)

    def _process_char(self, c):
        dialect = self._dialect
        state = self._state
        if state == _START_RECORD:
            if c is _EOL:
                return   # empty line - return []
            if c == '\n' or c == '\r':
                self._state = _EAT_CRNL
                return
            # normal character - handle as _START_FIELD
            state = self._state = _START_FIELD
        if state == _START_FIELD:
            self._unquoted_field = True
            if c == '\n' or c == '\r' or c is _EOL:
                self._save_field()
                self._state = _START_RECORD if c is _EOL else _EAT_CRNL
            elif c == dialect._quotechar and dialect._quoting != QUOTE_NONE:
                self._unquoted_field = False
                self._state = _IN_QUOTED_FIELD
            elif c == dialect._escapechar:
                self._state = _ESCAPED_CHAR
            elif c == ' ' and dialect._skipinitialspace:
                pass   # ignore spaces at start of field
            elif c == dialect._delimiter:
                self._save_field()
            else:
                self._add_char(c)
                self._state = _IN_FIELD
        elif state == _ESCAPED_CHAR:
            if c == '\n' or c == '\r':
                self._add_char(c)
                self._state = _AFTER_ESCAPED_CRNL
                return
            self._add_char('\n' if c is _EOL else c)
            self._state = _IN_FIELD
        elif state == _IN_FIELD or state == _AFTER_ESCAPED_CRNL:
            if state == _AFTER_ESCAPED_CRNL and c is _EOL:
                return
            if c == '\n' or c == '\r' or c is _EOL:
                self._save_field()
                self._state = _START_RECORD if c is _EOL else _EAT_CRNL
            elif c == dialect._escapechar:
                self._state = _ESCAPED_CHAR
            elif c == dialect._delimiter:
                self._save_field()
                self._state = _START_FIELD
            else:
                self._add_char(c)
        elif state == _IN_QUOTED_FIELD:
            if c is _EOL:
                pass
            elif c == dialect._escapechar:
                self._state = _ESCAPE_IN_QUOTED_FIELD
            elif c == dialect._quotechar and dialect._quoting != QUOTE_NONE:
                self._state = _QUOTE_IN_QUOTED_FIELD if dialect._doublequote else _IN_FIELD
            else:
                self._add_char(c)
        elif state == _ESCAPE_IN_QUOTED_FIELD:
            self._add_char('\n' if c is _EOL else c)
            self._state = _IN_QUOTED_FIELD
        elif state == _QUOTE_IN_QUOTED_FIELD:
            if dialect._quoting != QUOTE_NONE and c == dialect._quotechar:
                self._add_char(c)   # save "" as "
                self._state = _IN_QUOTED_FIELD
            elif c == dialect._delimiter:
                self._save_field()
                self._state = _START_FIELD
            elif c == '\n' or c == '\r' or c is _EOL:
                self._save_field()
                self._state = _START_RECORD if c is _EOL else _EAT_CRNL
            elif not dialect._strict:
                self._add_char(c)
                self._state = _IN_FIELD
            else:
                raise Error("'%s' expected after '%s'" % (dialect._delimiter, dialect._quotechar))
        elif state == _EAT_CRNL:
            if c == '\n' or c == '\r':
                pass
            elif c is _EOL:
                self._state = _START_RECORD
            else:
                raise Error("new-line character seen in unquoted field - "
                            "do you need to open the file with newline=''?")


Reader.__name__ = Reader.__qualname__ = 'reader'


def reader(iterable, dialect=_MISSING, /, **fmtparams):
    """Return a reader object that will process lines from the given iterable.

The "iterable" argument can be any object that returns a line
of input for each iteration, such as a file object or a list.  The
optional "dialect" argument defines a CSV dialect.  The function
also accepts optional keyword arguments which override settings
provided by the dialect.

The returned object is an iterator.  Each iteration returns a row
of the CSV file (which can span multiple input lines).
"""
    self = object.__new__(Reader)
    self._dialect = None
    self._line_num = 0
    self._parse_reset()
    self._input_iter = iter(iterable)
    self._dialect = _call_dialect(dialect, fmtparams)
    return self


# ---------------------------------------------------------------------------
# Writer.

def _is_number(ob):
    """PyNumber_Check."""
    tp = type(ob)
    return hasattr(tp, '__index__') or hasattr(tp, '__int__') or hasattr(tp, '__float__') or isinstance(ob, complex)


class Writer:
    """CSV writer

Writer objects are responsible for generating tabular data
in CSV format from sequence input.
"""

    __module__ = '_csv'
    __slots__ = ('_write', '_dialect', '_rec', '_num_fields')

    def __new__(cls, *args, **kwargs):
        _no_instances(cls)

    dialect = property(lambda self: self._dialect)

    def writerow(self, row, /):
        """Construct and write a CSV record from an iterable of fields.

Non-string elements will be converted to string."""
        dialect = self._dialect
        quoting = dialect._quoting
        try:
            fields = iter(row)
        except TypeError:
            fields = None
        if fields is None:
            raise Error('iterable expected, not %.200s' % _tp_name(type(row)))
        self._rec = []
        self._num_fields = 0
        null_field = False
        for field in fields:
            if quoting == QUOTE_NONNUMERIC:
                quoted = not _is_number(field)
            elif quoting == QUOTE_ALL:
                quoted = True
            elif quoting == QUOTE_STRINGS:
                quoted = isinstance(field, str)
            elif quoting == QUOTE_NOTNULL:
                quoted = field is not None
            else:
                quoted = False
            null_field = field is None
            if null_field:
                self._join_append(None, quoted)
            else:
                self._join_append(field if isinstance(field, str) else str(field), quoted)
        if self._num_fields > 0 and not self._rec:
            if quoting == QUOTE_NONE or (null_field and (quoting == QUOTE_STRINGS or quoting == QUOTE_NOTNULL)):
                raise Error('single empty field record must be quoted')
            self._num_fields -= 1
            self._join_append(None, True)
        self._rec.append(dialect._lineterminator)
        line = ''.join(self._rec)
        self._rec = None
        return self._write(line)

    def writerows(self, rows, /):
        """Construct and write a series of iterables to a csv file.

Non-string elements will be converted to string."""
        for row in rows:
            self.writerow(row)

    def _join_append(self, field, quoted):
        dialect = self._dialect
        if not field and dialect._delimiter == ' ' and dialect._skipinitialspace:
            if dialect._quoting == QUOTE_NONE or (
                    field is None and (dialect._quoting == QUOTE_STRINGS or dialect._quoting == QUOTE_NOTNULL)):
                raise Error('empty field must be quoted if delimiter is a space and skipinitialspace is true')
            quoted = True
        delimiter = dialect._delimiter
        escapechar = dialect._escapechar
        quotechar = dialect._quotechar
        lineterminator = dialect._lineterminator
        out = []
        for c in field or '':
            want_escape = False
            if (c == delimiter or c == escapechar or c == quotechar or c == '\n' or c == '\r'
                    or c in lineterminator):
                if dialect._quoting == QUOTE_NONE:
                    want_escape = True
                else:
                    if c == quotechar:
                        if dialect._doublequote:
                            out.append(quotechar)
                        else:
                            want_escape = True
                    elif c == escapechar:
                        want_escape = True
                    if not want_escape:
                        quoted = True
                if want_escape:
                    if escapechar is None:
                        raise Error('need to escape, but no escapechar set')
                    out.append(escapechar)
            out.append(c)
        rec = self._rec
        if self._num_fields > 0:
            rec.append(delimiter)
        if quoted:
            rec.append(quotechar)
        rec.extend(out)
        if quoted:
            rec.append(quotechar)
        self._num_fields += 1


Writer.__name__ = Writer.__qualname__ = 'writer'


def writer(fileobj, dialect=_MISSING, /, **fmtparams):
    """Return a writer object that will write user data on the given file object.

The "fileobj" argument can be any object that supports the file API.
The optional "dialect" argument defines a CSV dialect.  The function
also accepts optional keyword arguments which override settings
provided by the dialect.
"""
    self = object.__new__(Writer)
    self._dialect = None
    self._rec = None
    self._num_fields = 0
    try:
        write = fileobj.write
    except AttributeError:
        write = None
    if write is None or not callable(write):
        raise TypeError('argument 1 must have a "write" method')
    self._write = write
    self._dialect = _call_dialect(dialect, fmtparams)
    return self


# ---------------------------------------------------------------------------
# Dialect registry and field limit.

def list_dialects():
    """Return a list of all known dialect names."""
    return list(_dialects)


def register_dialect(name, dialect=_MISSING, /, **fmtparams):
    """Create a mapping from a string name to a CVS dialect.

The optional "dialect" argument specifies the base dialect instance
or the name of the registered dialect.  The function also accepts
optional keyword arguments which override settings provided by the
dialect.
"""
    if not isinstance(name, str):
        raise TypeError('dialect name must be a string')
    _dialects[name] = _call_dialect(dialect, fmtparams)


def unregister_dialect(name):
    """Delete the name/dialect mapping associated with a string name."""
    try:
        del _dialects[name]
    except KeyError:
        raise Error('unknown dialect') from None


def get_dialect(name):
    """Return the dialect instance associated with name."""
    return _get_dialect_from_registry(name)


def field_size_limit(new_limit=_MISSING):
    """Sets an upper limit on parsed fields.

Returns old limit. If limit is not given, no new limit is set and
the old limit is returned"""
    global _field_limit
    old_limit = _field_limit
    if new_limit is not _MISSING:
        if type(new_limit) is not int:
            raise TypeError('limit must be an integer')
        if new_limit > _SSIZE_MAX or new_limit < -_SSIZE_MAX - 1:
            raise OverflowError('Python int too large to convert to C ssize_t')
        _field_limit = new_limit
    return old_limit
