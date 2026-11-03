"""The builtins module's functions (Python/bltinmodule.c, Objects/abstract.c),
over the _lucifer primitives. Functions of positional-only parameters are
declared as such: the interpreter words their arity errors as a C builtin's.
Those with keywords parse them with _args, which words theirs as Argument
Clinic does."""

from _lucifer import (
    write, str_join, error, kind, type_lookup, type_tp_name, MISSING, static_type, is_callable,
    is_subtype, size, int_format, int_add, int_as_double, str_chr, str_ord, bytes_ord, str_len,
    object_id, caller_locals, caller_globals, eval_code, kernel_get, list_append,
)
from _lucifer import _boot, _format, operators, _args, iter as _iter
import sys

call_special = _boot.call_special
kernel_type = _boot.kernel_type
to_str = _format.to_str
index = operators.index
truth = operators.truth
decimal = operators.decimal
get_iter = _iter.get_iter
next_item = _iter.next_item
getattr_hook = kernel_get('getattr')
setattr_hook = kernel_get('setattr')
delattr_hook = kernel_get('delattr')

__all__ = (
    'print', 'isinstance', 'issubclass', 'len', 'hash', 'abs', 'all', 'any', 'bin', 'oct', 'hex',
    'callable', 'chr', 'ord', 'delattr', 'getattr', 'hasattr', 'setattr', 'dir', 'vars', 'divmod',
    'pow', 'round', 'id', 'sum', 'max', 'min', 'globals', 'locals', 'eval', 'exec', 'compile',
    'input', 'breakpoint', 'aiter', 'anext', '__import__',
)

_code = type((lambda: 0).__code__)


def _name(x):
    return type_tp_name(type(x))


def print(*args, sep=' ', end='\n', file=None, flush=False):
    """Prints the values to a stream, or to sys.stdout by default.

  sep
    string inserted between values, default a space.
  end
    string appended after the last value, default a newline.
  file
    a file-like object (stream); defaults to the current sys.stdout.
  flush
    whether to forcibly flush the stream."""
    if sep is None:
        sep = ' '
    elif not kind(sep, 'str'):
        error(TypeError, str_join('', ('sep must be None or a string, not ', _name(sep))))
    if end is None:
        end = '\n'
    elif not kind(end, 'str'):
        error(TypeError, str_join('', ('end must be None or a string, not ', _name(end))))
    if file is None:
        pieces = []
        try:
            for x in args:
                if pieces:
                    list_append(pieces, to_str(sep))
                list_append(pieces, to_str(x))
        except BaseException:
            write(1, str_join('', (*pieces,)))
            raise
        write(1, str_join('', (*pieces, to_str(end))))
        return None
    first = True
    for x in args:
        if not first:
            file.write(to_str(sep))
        first = False
        file.write(to_str(x))
    file.write(to_str(end))
    if flush:
        file.flush()


def _check(name, cls, inst, method, what):
    """PyObject_IsInstance and PyObject_IsSubclass: exact type, tuples of
    classes, then the metaclass's __instancecheck__ or __subclasscheck__."""
    if kind(cls, 'tuple'):
        for c in cls:
            if _check(name, c, inst, method, what):
                return True
        return False
    check = type_lookup(type(cls), method)
    if check is MISSING:
        error(TypeError, str_join('', (name, '() arg 2 must be a ', what)))
    return True if call_special(check, cls, inst) else False


def isinstance(obj, class_or_tuple, /):
    """Return whether an object is an instance of a class or of a subclass thereof."""
    if type(obj) is class_or_tuple:
        return True
    return _check('isinstance', class_or_tuple, obj, '__instancecheck__',
                  'type, a tuple of types, or a union')


def issubclass(cls, class_or_tuple, /):
    """Return whether 'cls' is derived from another class or is the same class."""
    return _check('issubclass', class_or_tuple, cls, '__subclasscheck__',
                  'class, a tuple of classes, or a union')


def len(obj, /):
    """Return the number of items in a container."""
    return operators.length(obj)


def hash(obj, /):
    """Return the hash value for the given object.

Two objects that compare equal must also have the same hash value, but the
reverse is not necessarily true."""
    return operators.hash_of(obj)


def abs(x, /):
    """Return the absolute value of the argument."""
    f = type_lookup(type(x), '__abs__')
    if f is MISSING:
        error(TypeError, str_join('', ("bad operand type for abs(): '", _name(x), "'")))
    return call_special(f, x)


def all(iterable, /):
    """Return True if bool(x) is True for all values x in the iterable.

If the iterable is empty, return True."""
    for x in iterable:
        if not x:
            return False
    return True


def any(iterable, /):
    """Return True if bool(x) is True for any x in the iterable.

If the iterable is empty, return False."""
    for x in iterable:
        if x:
            return True
    return False


def bin(number, /):
    """Return the binary representation of an integer.

   >>> bin(2796202)
   '0b1010101010101010101010'"""
    return int_format(index(number), 2, True)


def oct(number, /):
    """Return the octal representation of an integer.

   >>> oct(342391)
   '0o1234567'"""
    return int_format(index(number), 8, True)


def hex(number, /):
    """Return the hexadecimal representation of an integer.

   >>> hex(12648430)
   '0xc0ffee'"""
    return int_format(index(number), 16, True)


def callable(obj, /):
    """Return whether the object is callable (i.e., some kind of function).

Note that classes are callable, as are instances of classes with a
__call__() method."""
    return is_callable(obj)


def chr(i, /):
    """Return a Unicode string of one character with ordinal i; 0 <= i <= 0x10ffff."""
    n = index(i)
    if n > 2147483647 or n < -2147483648:
        error(OverflowError, 'Python int too large to convert to C int')
    if n < 0 or n > 0x10FFFF:
        error(ValueError, 'chr() arg not in range(0x110000)')
    return str_chr(n)


def ord(c, /):
    """Return the Unicode code point for a one-character string."""
    if kind(c, 'bytes') or kind(c, 'bytearray'):
        n = operators.length(c)
        if n == 1:
            return operators.getitem(c, 0) if kind(c, 'bytearray') else bytes_ord(c, 0)
    elif kind(c, 'str'):
        n = str_len(c)
        if n == 1:
            return str_ord(c, 0)
    else:
        error(TypeError, str_join('', ('ord() expected string of length 1, but ', _name(c), ' found')))
    error(TypeError, str_join('', (
        'ord() expected a character, but string of length ', decimal(n), ' found')))


def id(obj, /):
    """Return the identity of an object.

This is guaranteed to be unique among simultaneously existing objects.
(CPython uses the object's memory address.)"""
    return object_id(obj)


def _attribute_name(name):
    if not kind(name, 'str'):
        error(TypeError, str_join('', ('attribute name must be string, not ', "'", _name(name), "'")))


def getattr(obj, name, default=MISSING, /):
    """Get a named attribute from an object.

getattr(x, 'y') is equivalent to x.y
When a default argument is given, it is returned when the attribute doesn't
exist; without it, an exception is raised in that case."""
    _attribute_name(name)
    if default is MISSING:
        return getattr_hook(obj, name)
    try:
        return getattr_hook(obj, name)
    except AttributeError:
        return default


def hasattr(obj, name, /):
    """Return whether the object has an attribute with the given name.

This is done by calling getattr(obj, name) and catching AttributeError."""
    _attribute_name(name)
    try:
        getattr_hook(obj, name)
    except AttributeError:
        return False
    return True


def setattr(obj, name, value, /):
    """Sets the named attribute on the given object to the specified value.

setattr(x, 'y', v) is equivalent to ``x.y = v``"""
    _attribute_name(name)
    setattr_hook(obj, name, value)


def delattr(obj, name, /):
    """Deletes the named attribute from the given object.

delattr(x, 'y') is equivalent to ``del x.y``"""
    _attribute_name(name)
    delattr_hook(obj, name)


def vars(obj=MISSING, /):
    """Show vars.

Without arguments, equivalent to locals().
With an argument, equivalent to object.__dict__."""
    if obj is MISSING:
        return caller_locals()
    try:
        return getattr_hook(obj, '__dict__')
    except AttributeError:
        error(TypeError, 'vars() argument must have __dict__ attribute')


def dir(obj=MISSING, /):
    """Show attributes of an object.

If called without an argument, return the names in the current scope.
Else, return an alphabetized list of names comprising (some of) the attributes
of the given object, and of attributes reachable from it.
If the object supplies a method named __dir__, it will be used; otherwise
the default dir() logic is used and returns:
  for a module object: the module's attributes.
  for a class object:  its attributes, and recursively the attributes
    of its bases.
  for any other object: its attributes, its class's attributes, and
    recursively the attributes of its class's base classes."""
    if obj is MISSING:
        result = list(caller_locals())
    else:
        f = type_lookup(type(obj), '__dir__')
        if f is MISSING:
            error(TypeError, 'object does not provide __dir__')
        result = list(call_special(f, obj))
    result.sort()
    return result


def globals():
    """Return the dictionary containing the current scope's global variables."""
    return caller_globals()


def locals():
    """Return a dictionary containing the current scope's local variables."""
    return caller_locals()


def _binary(a, b, forward, reverse, label):
    """binary_op for the operators that BINARY_OP has no oparg for."""
    ta = type(a)
    tb = type(b)
    f = type_lookup(ta, forward)
    fb = MISSING if tb is ta else type_lookup(tb, reverse)
    if fb is f:
        fb = MISSING
    if fb is not MISSING and is_subtype(tb, ta) and fb is not type_lookup(ta, reverse):
        r = call_special(fb, b, a)
        if r is not NotImplemented:
            return r
        fb = MISSING
    if f is not MISSING:
        r = call_special(f, a, b)
        if r is not NotImplemented:
            return r
    if fb is not MISSING:
        r = call_special(fb, b, a)
        if r is not NotImplemented:
            return r
    error(TypeError, str_join('', (
        'unsupported operand type(s) for ', label, ": '", _name(a), "' and '", _name(b), "'")))


def divmod(x, y, /):
    """Return the tuple (x//y, x%y).  Invariant: div*y + mod == x."""
    return _binary(x, y, '__divmod__', '__rdivmod__', 'divmod()')


def pow(*args, **kwargs):
    """Equivalent to base**exp with 2 arguments or base**exp % mod with 3 arguments

Some types, such as ints, are able to use a more efficient algorithm when
invoked using the three argument form."""
    base, exp, mod = _args.unpack('pow', args, kwargs, ('base', 'exp', 'mod'), 0, 2)
    if mod is MISSING or mod is None:
        return operators.binary_op(base, exp, 8)
    f = type_lookup(type(base), '__pow__')
    if f is not MISSING:
        r = call_special(f, base, exp, mod)
        if r is not NotImplemented:
            return r
    error(TypeError, str_join('', (
        "unsupported operand type(s) for ** or pow(): '", _name(base), "', '", _name(exp), "', '",
        _name(mod), "'")))


def round(*args, **kwargs):
    """Round a number to a given precision in decimal digits.

The return value is an integer if ndigits is omitted or None.  Otherwise
the return value has the same type as the number.  ndigits may be negative."""
    number, ndigits = _args.unpack('round', args, kwargs, ('number', 'ndigits'), 0, 1)
    f = type_lookup(type(number), '__round__')
    if f is MISSING:
        error(TypeError, str_join('', ("type ", _name(number), " doesn't define __round__ method")))
    if ndigits is MISSING or ndigits is None:
        return call_special(f, number)
    return call_special(f, number, ndigits)


def _compensated(hi, lo, x):
    """cs_add of Python/bltinmodule.c: Neumaier's running sum."""
    t = hi + x
    if (hi if hi >= 0 else -hi) >= (x if x >= 0 else -x):
        lo += (hi - t) + x
    else:
        lo += (x - t) + hi
    return t, lo


def _total(hi, lo):
    """cs_to_double: the compensation never turns an overflow into a NaN."""
    if lo != 0 and lo - lo == 0:
        return hi + lo
    return hi


def sum(*args, **kwargs):
    """Return the sum of a 'start' value (default: 0) plus an iterable of numbers

When the iterable is empty, return the start value.
This function is intended specifically for use with numeric values and
may reject non-numeric types."""
    iterable, start = _args.unpack('sum', args, kwargs, ('iterable', 'start'), 1, 1)
    it = get_iter(iterable)
    if start is MISSING:
        result = 0
    else:
        if kind(start, 'str'):
            error(TypeError, "sum() can't sum strings [use ''.join(seq) instead]")
        if kind(start, 'bytes'):
            error(TypeError, "sum() can't sum bytes [use b''.join(seq) instead]")
        if kind(start, 'bytearray'):
            error(TypeError, "sum() can't sum bytearray [use b''.join(seq) instead]")
        result = start
    if type(result) is int:
        while True:
            item = next_item(it)
            if item is MISSING:
                return result
            if type(item) is int or type(item) is bool:
                result = int_add(result, item)
            else:
                result = result + item
                break
    if type(result) is float:
        hi = result
        lo = 0.0
        while True:
            item = next_item(it)
            if item is MISSING:
                return _total(hi, lo)
            if type(item) is float:
                hi, lo = _compensated(hi, lo, item)
            elif kind(item, 'int'):
                hi, lo = _compensated(hi, lo, int_as_double(item))
            else:
                result = _total(hi, lo) + item
                break
    if type(result) is complex:
        re_hi = result.real
        im_hi = result.imag
        re_lo = im_lo = 0.0
        while True:
            item = next_item(it)
            if item is MISSING:
                return complex(_total(re_hi, re_lo), _total(im_hi, im_lo))
            if type(item) is complex:
                re_hi, re_lo = _compensated(re_hi, re_lo, item.real)
                im_hi, im_lo = _compensated(im_hi, im_lo, item.imag)
            elif kind(item, 'int'):
                re_hi, re_lo = _compensated(re_hi, re_lo, int_as_double(item))
            elif kind(item, 'float'):
                re_hi, re_lo = _compensated(re_hi, re_lo, item.real)
            else:
                result = complex(_total(re_hi, re_lo), _total(im_hi, im_lo)) + item
                break
    while True:
        item = next_item(it)
        if item is MISSING:
            return result
        result = result + item


def _min_max(name, args, kwargs, greater):
    if not args:
        error(TypeError, str_join('', (name, ' expected at least 1 argument, got 0')))
    key = default = MISSING
    for k, v in kwargs.items():
        if k == 'key':
            key = v
        elif k == 'default':
            default = v
        else:
            error(TypeError, str_join('', (name, "() got an unexpected keyword argument '", k, "'")))
    positional = size(args) > 1
    if positional and default is not MISSING:
        error(TypeError, str_join('', (
            'Cannot specify a default for ', name, '() with multiple positional arguments')))
    it = get_iter(args if positional else args[0])
    if key is None:
        key = MISSING
    best = bestval = MISSING
    while True:
        item = next_item(it)
        if item is MISSING:
            break
        val = item if key is MISSING else key(item)
        if bestval is MISSING:
            best = item
            bestval = val
        elif (val > bestval) if greater else (val < bestval):
            best = item
            bestval = val
    if bestval is MISSING:
        if default is MISSING:
            error(ValueError, str_join('', (name, '() iterable argument is empty')))
        return default
    return best


def max(*args, **kwargs):
    """max(iterable, *[, default=obj, key=func]) -> value
max(arg1, arg2, *args, *[, key=func]) -> value

With a single iterable argument, return its biggest item. The
default keyword-only argument specifies an object to return if
the provided iterable is empty.
With two or more positional arguments, return the largest argument."""
    return _min_max('max', args, kwargs, True)


def min(*args, **kwargs):
    """min(iterable, *[, default=obj, key=func]) -> value
min(arg1, arg2, *args, *[, key=func]) -> value

With a single iterable argument, return its smallest item. The
default keyword-only argument specifies an object to return if
the provided iterable is empty.
With two or more positional arguments, return the smallest argument."""
    return _min_max('min', args, kwargs, False)


def _source(fname, source, globals_, locals_):
    """The code object of exec's and eval's source, and the namespaces it
    runs in: those of the calling frame unless given."""
    if locals_ is not None and type_lookup(type(locals_), '__getitem__') is MISSING:
        error(TypeError, str_join('', (
            'locals must be a mapping' if fname == 'eval' else 'locals must be a mapping or None, not ',
            '' if fname == 'eval' else _name(locals_))))
    if globals_ is not None and not kind(globals_, 'dict'):
        if fname == 'eval':
            error(TypeError, 'globals must be a real dict; try eval(expr, {}, mapping)'
                  if type_lookup(type(globals_), '__getitem__') is not MISSING
                  else 'globals must be a dict')
        error(TypeError, str_join('', ('exec() globals must be a dict, not ', _name(globals_))))
    if globals_ is None:
        globals_ = caller_globals()
        if locals_ is None:
            locals_ = caller_locals()
    elif locals_ is None:
        locals_ = globals_
    if type(source) is not _code:
        if kind(source, 'str') or kind(source, 'bytes') or kind(source, 'bytearray'):
            raise NotImplementedError(str_join('', (fname, '() of source text waits for the compiler')))
        error(TypeError, str_join('', (fname, '() arg 1 must be a string, bytes or code object')))
    if not kind(locals_, 'dict'):
        raise NotImplementedError(str_join('', (fname, '() with a locals mapping that is not a dict')))
    return globals_, locals_


def eval(*args, **kwargs):
    """Evaluate the given source in the context of globals and locals.

The source may be a string representing a Python expression
or a code object as returned by compile().
The globals must be a dictionary and locals can be any mapping,
defaulting to the current globals and locals.
If only globals is given, locals defaults to it."""
    source, globals_, locals_ = _args.unpack('eval', args, kwargs, ('source', 'globals', 'locals'), 1, 1)
    globals_, locals_ = _source('eval', source, None if globals_ is MISSING else globals_,
                                None if locals_ is MISSING else locals_)
    return eval_code(source, globals_, locals_)


def exec(*args, **kwargs):
    """Execute the given source in the context of globals and locals.

The source may be a string representing one or more Python statements
or a code object as returned by compile().
The globals must be a dictionary and locals can be any mapping,
defaulting to the current globals and locals.
If only globals is given, locals defaults to it."""
    source, globals_, locals_ = _args.unpack('exec', args, kwargs, ('source', 'globals', 'locals'), 1, 1)
    globals_, locals_ = _source('exec', source, None if globals_ is MISSING else globals_,
                                None if locals_ is MISSING else locals_)
    eval_code(source, globals_, locals_)
    return None


def compile(source, filename, mode, flags=0, dont_inherit=False, optimize=-1, *, _feature_version=-1):
    """Compile source into a code object that can be executed by exec() or eval()."""
    raise NotImplementedError('compile() waits for the runtime compiler')


def __import__(name, globals=None, locals=None, fromlist=(), level=0):
    """Import a module.  The importlib bootstrap replaces this placeholder."""
    raise ModuleNotFoundError(str_join('', ("No module named '", name, "'")), name=name)


def breakpoint(*args, **kwargs):
    """Call sys.breakpointhook(*args, **kws).  sys.breakpointhook() must accept
whatever arguments are passed.

By default, this drops you into the pdb debugger."""
    try:
        hook = getattr_hook(sys, 'breakpointhook')
    except AttributeError:
        raise RuntimeError('lost sys.breakpointhook')
    return hook(*args, **kwargs)


def _stream(name):
    try:
        stream = getattr_hook(sys, name)
    except AttributeError:
        stream = None
    if stream is None:
        error(RuntimeError, str_join('', ('input(): lost sys.', name)))
    return stream


def input(prompt=MISSING, /):
    """Read a string from standard input.  The trailing newline is stripped.

The prompt string, if given, is printed to standard output without a
trailing newline before reading input.

If the user hits EOF (*nix: Ctrl-D, Windows: Ctrl-Z+Return), raise EOFError.
On *nix systems, readline is used if available."""
    stdin = _stream('stdin')
    stdout = _stream('stdout')
    if prompt is not MISSING:
        stdout.write(to_str(prompt))
    stdout.flush()
    line = stdin.readline()
    if not kind(line, 'str'):
        error(TypeError, 'object.readline() returned non-string')
    if not line:
        error(EOFError, 'EOF when reading a line')
    return line[:-1] if line[-1] == '\n' else line


def aiter(async_iterable, /):
    """Return an AsyncIterator for an AsyncIterable object."""
    f = type_lookup(type(async_iterable), '__aiter__')
    if f is MISSING:
        error(TypeError, str_join('', ("'", _name(async_iterable), "' object is not an async iterable")))
    it = call_special(f, async_iterable)
    if type_lookup(type(it), '__anext__') is MISSING:
        error(TypeError, str_join('', (
            'aiter() returned not an async iterator of type ', "'", _name(it), "'")))
    return it


async def _anext_default(awaitable, default):
    try:
        return await awaitable
    except StopAsyncIteration:
        return default


def anext(aiterator, default=MISSING, /):
    """Return the next item from the async iterator.

If default is given and the async iterator is exhausted,
it is returned instead of raising StopAsyncIteration."""
    f = type_lookup(type(aiterator), '__anext__')
    if f is MISSING:
        error(TypeError, str_join('', ("'", _name(aiterator), "' object is not an async iterator")))
    awaitable = call_special(f, aiterator)
    if default is MISSING:
        return awaitable
    return _anext_default(awaitable, default)


@kernel_type(static_type('NoneType'))
class NoneType:
    def __hash__(self):
        return 0xFCA86420           # _Py_HashPointer is not used for None since 3.12

    def __repr__(self):
        return 'None'


@kernel_type(static_type('NotImplementedType'))
class NotImplementedType:
    def __repr__(self):
        return 'NotImplemented'
