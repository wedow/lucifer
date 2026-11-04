"""The builtins module's functions (Python/bltinmodule.c, Objects/abstract.c),
over the _lucifer primitives. Functions of positional-only parameters are
declared as such: the interpreter words their arity errors as a C builtin's.
Those with keywords parse them with _args, which words theirs as Argument
Clinic does."""

from _lucifer import (
    str_join, error, kind, type_lookup, type_tp_name, MISSING, static_type, is_callable,
    is_subtype, size, int_format, int_add, int_as_double, str_chr, str_ord, bytes_ord, str_len,
    object_id, caller_locals, caller_globals, caller_code_flags, eval_code, kernel_get, kernel_set,
    import_state, field_get, setattr_native,
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
_Union = static_type('typing.Union')


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
        try:
            file = getattr_hook(sys, 'stdout')
        except AttributeError:
            error(RuntimeError, 'lost sys.stdout')
        if file is None:
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
    if type(cls) is _Union:
        cls = field_get(cls, 0)
    if kind(cls, 'tuple'):
        for c in cls:
            if _check(name, c, inst, method, what):
                return True
        return False
    check = type_lookup(type(cls), method)
    if check is MISSING:
        error(TypeError, str_join('', (name, '() arg 2 must be a ', what)))
    return True if call_special(check, cls, inst) else False


_object_class = type_lookup(object, '__class__')
_object_getattribute = type_lookup(object, '__getattribute__')


def isinstance(obj, class_or_tuple, /):
    """Return whether an object is an instance of a class or of a subclass thereof."""
    t = type(obj)
    if t is class_or_tuple:
        return True
    if type(class_or_tuple) is type:
        if is_subtype(t, class_or_tuple):
            return True
        # A negative answer asks obj.__class__ (object_isinstance).
        if type_lookup(t, '__class__') is _object_class and type_lookup(t, '__getattribute__') is _object_getattribute:
            return False
    return _check('isinstance', class_or_tuple, obj, '__instancecheck__',
                  'type, a tuple of types, or a union')


def issubclass(cls, class_or_tuple, /):
    """Return whether 'cls' is derived from another class or is the same class."""
    return _check('issubclass', class_or_tuple, cls, '__subclasscheck__',
                  'class, a tuple of classes, or a union')


def len(obj, /):
    """Return the number of items in a container."""
    t = type(obj)
    if t is list or t is tuple or t is dict or t is str or t is set or t is frozenset or t is bytes:
        return size(obj)
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
    if setattr_native(obj, name, value):
        return
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


_PyCF_SOURCE_IS_UTF8 = 0x100
_PyCF_ONLY_AST = 0x400
_PyCF_OPTIMIZED_AST = 0x8400
_PyCF_MASK = 0x1fe0000                  # the CO_FUTURE_* flags
_PyCF_COMPILE_MASK = 0x400 | 0x2000 | 0x1000 | 0x200 | 0x4000 | 0x8400
_CO_NESTED = 0x10
_compiler = None


def _compile(source, filename, mode, flags, dont_inherit, optimize, feature_version):
    """The compiler's compile(); the compiler is imported on first use."""
    global _compiler
    if _compiler is None:
        import compiler
        _compiler = compiler
    return _compiler.compile(source, filename, mode, flags, dont_inherit, optimize,
                             _feature_version=feature_version)


def _source_text(fname, source):
    """The text of source for the compiler, or None when it is not source text:
    a str as it is, bytes-like objects as bytes (_Py_SourceAsString)."""
    if kind(source, 'str') or kind(source, 'bytes'):
        return source
    if kind(source, 'bytearray') or kind(source, 'memoryview'):
        return bytes(source)
    return None


def _code_of(fname, source, mode):
    """The code object of exec's or eval's source text: compiled as
    PyRun_StringFlags does, with the caller's future statements."""
    flags = caller_code_flags() & _PyCF_MASK
    sys.audit('compile', source, '<string>')
    if kind(source, 'str'):
        flags |= _PyCF_SOURCE_IS_UTF8
        if mode == 'eval':
            source = source.lstrip(' \t')
    elif mode == 'eval':
        source = source.lstrip(b' \t')
    return _compile(source, '<string>', mode, flags, False, -1, -1)


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
        text = _source_text(fname, source)
        if text is None:
            error(TypeError, str_join('', (fname, '() arg 1 must be a string, bytes or code object')))
        source = _code_of(fname, text, fname)
    if not kind(locals_, 'dict'):
        raise NotImplementedError(str_join('', (fname, '() with a locals mapping that is not a dict')))
    return source, globals_, locals_


def eval(*args, **kwargs):
    """Evaluate the given source in the context of globals and locals.

The source may be a string representing a Python expression
or a code object as returned by compile().
The globals must be a dictionary and locals can be any mapping,
defaulting to the current globals and locals.
If only globals is given, locals defaults to it."""
    source, globals_, locals_ = _args.unpack('eval', args, kwargs, ('source', 'globals', 'locals'), 1, 1)
    source, globals_, locals_ = _source('eval', source, None if globals_ is MISSING else globals_,
                                        None if locals_ is MISSING else locals_)
    sys.audit('exec', source)
    return eval_code(source, globals_, locals_)


def exec(*args, **kwargs):
    """Execute the given source in the context of globals and locals.

The source may be a string representing one or more Python statements
or a code object as returned by compile().
The globals must be a dictionary and locals can be any mapping,
defaulting to the current globals and locals.
If only globals is given, locals defaults to it."""
    source, globals_, locals_ = _args.unpack('exec', args, kwargs, ('source', 'globals', 'locals'), 1, 1)
    source, globals_, locals_ = _source('exec', source, None if globals_ is MISSING else globals_,
                                        None if locals_ is MISSING else locals_)
    sys.audit('exec', source)
    eval_code(source, globals_, locals_)
    return None


def _filename(filename):
    """PyUnicode_FSDecoder"""
    if kind(filename, 'str'):
        return filename
    if kind(filename, 'bytes'):
        return filename.decode('utf-8', 'surrogateescape')
    fspath = type_lookup(type(filename), '__fspath__')
    if fspath is not MISSING:
        path = fspath(filename)
        if kind(path, 'str'):
            return path
        if kind(path, 'bytes'):
            return path.decode('utf-8', 'surrogateescape')
        error(TypeError, str_join('', (
            'expected ', _name(filename), '.__fspath__() to return str or bytes, not ', _name(path))))
    error(TypeError, str_join('', (
        "compile() argument 'filename' must be str, bytes or os.PathLike, not ", _name(filename))))


def compile(*args, **kwargs):
    """Compile source into a code object that can be executed by exec() or eval().

The source code may represent a Python module, statement or expression.
The filename will be used for run-time error messages.
The mode must be 'exec' to compile a module, 'single' to compile a
single (interactive) statement, or 'eval' to compile an expression.
The flags argument, if present, controls which future statements influence
the compilation of the code.
The dont_inherit argument, if true, stops the compilation inheriting
the effects of any future statements in effect in the code calling
compile; if absent or false these statements do influence the compilation,
in addition to any features explicitly specified."""
    if size(args) > 6:
        error(TypeError, str_join('', ('compile() takes at most 6 positional arguments (',
                                       decimal(size(args)), ' given)')))
    source, filename, mode, flags, dont_inherit, optimize, feature_version = _args.unpack(
        'compile', args, kwargs, ('source', 'filename', 'mode', 'flags', 'dont_inherit', 'optimize',
                                  '_feature_version'), 0, 3)
    flags = 0 if flags is MISSING else flags
    dont_inherit = False if dont_inherit is MISSING else dont_inherit
    optimize = -1 if optimize is MISSING else optimize
    feature_version = -1 if feature_version is MISSING else index(feature_version)
    filename = _filename(filename)
    if not kind(mode, 'str'):
        error(TypeError, str_join('', ("compile() argument 'mode' must be str, not ", _name(mode))))
    flags = index(flags)
    optimize = index(optimize)
    if not -0x80000000 <= flags <= 0x7fffffff or not -0x80000000 <= optimize <= 0x7fffffff:
        error(OverflowError, 'Python int too large to convert to C int')
    if flags & ~(_PyCF_MASK | _CO_NESTED | _PyCF_COMPILE_MASK):
        error(ValueError, 'compile(): unrecognised flags')
    if optimize < -1 or optimize > 2:
        error(ValueError, 'compile(): invalid optimize value')
    if not truth(dont_inherit):
        flags |= caller_code_flags() & _PyCF_MASK
    if mode not in ('exec', 'eval', 'single', 'func_type'):
        error(ValueError, "compile() mode must be 'exec', 'eval' or 'single'")
    if mode == 'func_type' and not flags & _PyCF_ONLY_AST:
        error(ValueError, "compile() mode 'func_type' requires flag PyCF_ONLY_AST")
    import _ast
    if not isinstance(source, _ast.AST):
        text = _source_text('compile', source)
        if text is None:
            error(TypeError, 'compile() arg 1 must be a string, bytes or AST object')
        source = text
    sys.audit('compile', source, filename)
    return _compile(source, filename, mode, flags, truth(dont_inherit), optimize, feature_version)


def _resolve_name(name, globals_, level):
    """Python/import.c resolve_name: the absolute name of a relative import."""
    if not isinstance(globals_, dict):
        error(ImportError if globals_ is None else TypeError,
              'attempted relative import with no known parent package' if globals_ is None
              else 'globals must be a dict')
    package = globals_.get('__package__')
    spec = globals_.get('__spec__')
    if package is not None:
        if not isinstance(package, str):
            error(TypeError, 'package must be a string')
    elif spec is not None:
        package = spec.parent
        if not isinstance(package, str):
            error(TypeError, '__spec__.parent must be a string')
    else:
        if '__name__' not in globals_:
            error(KeyError, "'__name__' not in globals")
        package = globals_['__name__']
        if not isinstance(package, str):
            error(TypeError, '__name__ must be a string')
        if '__path__' not in globals_:
            package = package.rpartition('.')[0]
    last_dot = len(package)
    if last_dot == 0:
        error(ImportError, 'attempted relative import with no known parent package')
    for _ in range(level - 1):
        last_dot = package.rfind('.', 0, last_dot)
        if last_dot == -1:
            error(ImportError, 'attempted relative import beyond top-level package')
    base = package[:last_dot]
    return base + '.' + name if name else base


_IMPORTLIB_FILES = ('<frozen importlib._bootstrap>', '<frozen importlib._bootstrap_external>')


def _remove_importlib_frames(exc):
    """Python/import.c remove_importlib_frames: an ImportError loses every
    importlib frame, anything else the ones between _call_with_frames_removed
    and its callee. A link is the traceback entry whose tb_next points at the
    next one, or None for the exception's own __traceback__."""
    always = isinstance(exc, ImportError)

    def relink(holder, target):
        if holder is None:
            exc.__traceback__ = target
        else:
            holder.tb_next = target

    tb = exc.__traceback__
    previous = outer = None
    in_importlib = False
    while tb is not None:
        following = tb.tb_next
        code = tb.tb_frame.f_code
        now_in_importlib = code.co_filename in _IMPORTLIB_FILES
        if now_in_importlib and not in_importlib:
            outer = previous
        in_importlib = now_in_importlib
        if in_importlib and (always or code.co_name == '_call_with_frames_removed'):
            relink(outer, following)
            previous = outer
        else:
            previous = tb
        tb = following


def _initializing(module):
    """Is the module's spec still being loaded (_PyModuleSpec_IsInitializing)?"""
    try:
        spec = getattr_hook(module, '__spec__')
    except AttributeError:
        return False
    if spec is None:
        return False
    try:
        return truth(getattr_hook(spec, '_initializing'))
    except AttributeError:
        return False


def _import(name, globals_, fromlist, level):
    """Python/import.c PyImport_ImportModuleLevelObject."""
    if level < 0:
        error(ValueError, 'level must be >= 0')
    if level > 0:
        abs_name = _resolve_name(name, globals_, level)
    else:
        if not name:
            error(ValueError, 'Empty module name')
        abs_name = name
    importlib, import_func = import_state()
    mod = sys.modules.get(abs_name)
    if mod is not None:
        if _initializing(mod):
            importlib._lock_unlock_module(abs_name)
    else:
        if importlib is None:
            error(ModuleNotFoundError, str_join('', ("No module named '", abs_name, "'")))
        sys.audit('import', abs_name, None, sys.path, sys.meta_path, sys.path_hooks)
        mod = importlib._find_and_load(abs_name, import_func)
    if not truth(fromlist):
        if level == 0 or name:
            dot = name.find('.')
            if dot == -1:
                return mod
            if level == 0:
                return _import(name[:dot], None, None, 0)
            to_return = abs_name[:len(abs_name) - (len(name) - dot)]
            if to_return not in sys.modules:
                error(KeyError, str_join('', (repr(to_return), ' not in sys.modules as expected')))
            return sys.modules[to_return]
        return mod
    try:
        getattr_hook(mod, '__path__')
    except AttributeError:
        return mod
    return importlib._handle_fromlist(mod, fromlist, import_func)


def __import__(name, globals=None, locals=None, fromlist=(), level=0):
    """Import a module.

The globals are only used to determine the context;
they are not modified.  The locals are currently unused.  The fromlist
should be a list of names to emulate ``from name import ...``, or an
empty list to emulate ``import name``.
When importing a module from a package, note that __import__('A.B', ...)
returns package A when fromlist is empty, but its submodule B when
fromlist is not empty.  The level argument is used to determine whether to
perform absolute or relative imports: 0 is absolute, while a positive number
is the number of parent directories to search relative to the current module."""
    if not isinstance(name, str):
        error(TypeError, 'module name must be a string')
    level = index(level)
    try:
        return _import(name, globals, fromlist, level)
    except BaseException as exc:
        _remove_importlib_frames(exc)
        raise


def import_from(module, name):
    """Python/ceval.c import_from: module.name, else the submodule of that
    name in sys.modules, else an ImportError that says where the module is."""
    try:
        return getattr_hook(module, name)
    except AttributeError:
        pass
    try:
        package = getattr_hook(module, '__name__')
    except AttributeError:
        package = None
    if isinstance(package, str):
        found = sys.modules.get(package + '.' + name)
        if found is not None:
            return found
    if not isinstance(package, str):
        exc = ImportError(str_join('', ('cannot import name ', repr(name))))
    else:
        origin = None
        try:
            spec = getattr_hook(module, '__spec__')
            if spec is not None and truth(getattr_hook(spec, 'has_location')):
                origin = getattr_hook(spec, 'origin')
        except AttributeError:
            pass
        if not isinstance(origin, str):
            origin = None
        text = str_join('', ('cannot import name ', repr(name), ' from '))
        if _initializing(module):
            text = str_join('', (text, 'partially initialized module ', repr(package),
                                 ' (most likely due to a circular import)'))
            if origin is not None:
                text = str_join('', (text, ' (', origin, ')'))
        elif origin is None:
            text = str_join('', (text, repr(package), ' (unknown location)'))
        else:
            text = str_join('', (text, repr(package), ' (', origin, ')'))
        exc = ImportError(text, name=package, path=origin)
        exc.name_from = name
    raise exc


kernel_set('import_from', import_from)


def import_star(namespace, module):
    """Python/ceval.c import_all_from: from module import *, into namespace."""
    skip_underscores = False
    try:
        names = getattr_hook(module, '__all__')
    except AttributeError:
        try:
            names = getattr_hook(module, '__dict__').keys()
        except AttributeError:
            raise ImportError('from-import-* object has no __dict__ and no __all__') from None
        skip_underscores = True
    which = '__dict__' if skip_underscores else '__all__'
    for name in [*names]:
        if not isinstance(name, str):
            try:
                owner = getattr_hook(module, '__name__')
            except AttributeError:
                owner = None
            raise TypeError(str_join('', (
                'Key in ' if skip_underscores else 'Item in ',
                '<module>' if not isinstance(owner, str) else owner, '.', which,
                ' must be str, not ', _name(name))))
        if skip_underscores and name[:1] == '_':
            continue
        namespace[name] = getattr_hook(module, name)
    return None


kernel_set('import_star', import_star)


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
        error(RuntimeError, str_join('', ('lost sys.', name)))
    return stream


def input(prompt=MISSING, /):
    """Read a string from standard input.  The trailing newline is stripped.

The prompt string, if given, is printed to standard output without a
trailing newline before reading input.

If the user hits EOF (*nix: Ctrl-D, Windows: Ctrl-Z+Return), raise EOFError.
On *nix systems, readline is used if available."""
    stdin = _stream('stdin')
    stdout = _stream('stdout')
    stderr = _stream('stderr')
    try:
        stderr.flush()
    except BaseException:
        pass
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


kernel_set('len', len)
kernel_set('builtin_getattr', getattr)
kernel_set('isinstance', isinstance)
