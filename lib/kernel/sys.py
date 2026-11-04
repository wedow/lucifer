"""The sys module (Python/sysmodule.c): its values and functions, then
configure(config), which Py_InitializeFromConfig calls with what the
PyConfig says (runtime/interp/sysmodule.c builds the dict).

sys itself is made natively before the kernel, with modules, the recursion
limit and int digits; this module adds the rest. Functions are installed as
builtins of sys, as C functions are, so their frames never show.
"""

from _lucifer import ns_set, ns_get, instance_dict, builtin_function, frame_at, MISSING
from _lucifer import os_environ, os_fstat as fstat, locale_encoding
from _lucifer import exceptions, generators, structseq, write_unraisable, err_display
import sys

__all__ = ()

_dict = instance_dict(sys, True)
_structseq = structseq.structseq_type
_make = structseq.structseq_make


def _install(*functions):
    for f in functions:
        ns_set(_dict, f.__name__, builtin_function(sys, f.__name__, f))


# namespace (Python/instrumentation.c's SimpleNamespace, Objects/namespaceobject.c)

def _namespace_init(self, mapping_or_iterable=(), /, **kwargs):
    self.__dict__.update(mapping_or_iterable)
    self.__dict__.update(kwargs)


def _namespace_repr(self):
    parts = ['%s=%r' % (k, v) for k, v in self.__dict__.items()]
    return 'namespace(' + ', '.join(parts) + ')'


def _namespace_eq(self, other):
    if isinstance(other, SimpleNamespace):
        return self.__dict__ == other.__dict__
    return NotImplemented


def _namespace_reduce(self):
    return (type(self), (), self.__dict__)


def _namespace_replace(self, **changes):
    result = type(self)()
    result.__dict__.update(self.__dict__)
    result.__dict__.update(changes)
    return result


SimpleNamespace = type('SimpleNamespace', (object,), {
    '__module__': 'types', '__init__': _namespace_init, '__repr__': _namespace_repr,
    '__eq__': _namespace_eq, '__reduce__': _namespace_reduce, '__replace__': _namespace_replace,
    '__hash__': None, '__doc__': 'A simple attribute-based namespace.',
})


# Values that never change

version_info = _make(_structseq(
    'sys.version_info', 'sys.version_info\n\nVersion information as a named tuple.',
    ('major', 'minor', 'micro', 'releaselevel', 'serial'), 5, False), (3, 14, 10, 'final', 0))
hexversion = 0x030E0AF0

implementation = SimpleNamespace(
    name='cpython', cache_tag='cpython-314', version=version_info, hexversion=hexversion,
    _multiarch='x86_64-linux-gnu', supports_isolated_interpreters=True,
    lucifer=(0, 1, 0))

hash_info = _make(_structseq(
    'sys.hash_info', 'hash_info\n\nA named tuple providing parameters used for computing\n'
    'hashes. The attributes are read only.',
    ('width', 'modulus', 'inf', 'nan', 'imag', 'algorithm', 'hash_bits', 'seed_bits', 'cutoff'),
    9, False), (64, 2305843009213693951, 314159, 0, 1000003, 'siphash13', 64, 128, 0))

int_info = _make(_structseq(
    'sys.int_info', 'sys.int_info\n\nA named tuple that holds information about Python\'s\n'
    'internal representation of integers.  The attributes are read only.',
    ('bits_per_digit', 'sizeof_digit', 'default_max_str_digits', 'str_digits_check_threshold'),
    4, False), (30, 4, 4300, 640))

float_info = _make(_structseq(
    'sys.float_info', 'sys.float_info\n\nA named tuple holding information about the float type. It contains '
    'low level\ninformation about the precision and internal representation. Please study\n'
    'your system\'s :file:`float.h` for more information.',
    ('max', 'max_exp', 'max_10_exp', 'min', 'min_exp', 'min_10_exp', 'dig', 'mant_dig',
     'epsilon', 'radix', 'rounds'), 11, False),
    (1.7976931348623157e+308, 1024, 308, 2.2250738585072014e-308, -1021, -307, 15, 53,
     2.220446049250313e-16, 2, 1))

thread_info = _make(_structseq(
    'sys.thread_info', 'sys.thread_info\n\nA named tuple holding information about the thread implementation.',
    ('name', 'lock', 'version'), 3, False), ('pthread', 'semaphore', None))

_asyncgen_hooks = _structseq(
    'asyncgen_hooks', 'asyncgen_hooks\n\nA named tuple providing information about asynchronous\n'
    'generators hooks.  The attributes are read only.', ('firstiter', 'finalizer'), 2, False)
_asyncgen_hooks.__module__ = 'sys'

for _name, _value in (
        ('abiflags', 't'), ('api_version', 1013), ('byteorder', 'little'),
        ('float_repr_style', 'short'), ('maxsize', 9223372036854775807), ('maxunicode', 1114111),
        ('platform', 'linux'), ('platlibdir', 'lib'), ('hexversion', hexversion),
        ('version', '3.14.10 free-threading build (lucifer) [lucifer]'),
        ('version_info', version_info), ('implementation', implementation),
        ('hash_info', hash_info), ('int_info', int_info), ('float_info', float_info),
        ('thread_info', thread_info), ('_framework', ''), ('_git', ('CPython', '', '')),
        ('copyright', 'Copyright (c) 2001 Python Software Foundation.\nAll Rights Reserved.'),
        ('meta_path', []), ('path_hooks', []), ('path_importer_cache', {})):
    ns_set(_dict, _name, _value)


# Functions

def exit(status=None, /):
    """Exit the interpreter by raising SystemExit(status).

If the status is omitted or None, it defaults to zero (i.e., success).
If the status is an integer, it will be used as the system exit status.
If it is another kind of object, it will be printed and the system
exit status will be one (i.e., failure)."""
    if status is None:
        raise SystemExit()
    if isinstance(status, tuple):
        raise SystemExit(*status)
    raise SystemExit(status)


def getdefaultencoding():
    """Return the current default encoding used by the Unicode implementation."""
    return 'utf-8'


def getfilesystemencoding():
    """Return the encoding used to convert Unicode filenames to OS filenames."""
    return 'utf-8'


def getfilesystemencodeerrors():
    """Return the error mode used Unicode to OS filename conversion."""
    return 'surrogateescape'


def getswitchinterval():
    """Return the current thread switch interval; see sys.setswitchinterval()."""
    return 0.005


def _is_gil_enabled():
    """Return True if the GIL is currently enabled and False otherwise."""
    return False


def get_asyncgen_hooks():
    """Return the installed asynchronous generators hooks.

This returns a namedtuple of the form (firstiter, finalizer)."""
    return _make(_asyncgen_hooks, generators.asyncgen_hooks())


_audit_hooks = []


def addaudithook(hook):
    """Adds a new audit hook callback."""
    if not _audit_hooks:
        # The compiler loads on first use; its imports are not the program's.
        compile('0', '<compiler>', 'eval')
    _audit_hooks.append(hook)


def audit(event, /, *args):
    """Passes the event to any audit hooks that are attached."""
    for hook in _audit_hooks:
        hook(event, args)


def _getframe(depth=0, /):
    """Return a frame object from the call stack."""
    f = frame_at(depth)
    if f is None:
        raise ValueError('call stack is not deep enough')
    audit('sys._getframe', f)
    return f


def excepthook(exctype, value, traceback, /):
    """Handle an exception by displaying it with a traceback on sys.stderr."""
    if isinstance(value, BaseException) and value.__traceback__ is not traceback:
        value.__traceback__ = traceback
    err_display(value)


def displayhook(object, /):
    """Print an object to sys.stdout and also save it in builtins._"""
    if object is None:
        return
    builtins = __import__('builtins')
    ns_set(instance_dict(builtins, True), '_', None)
    text = repr(object)
    stdout = ns_get(_dict, 'stdout', None)
    if stdout is None:
        raise RuntimeError('lost sys.stdout')
    try:
        stdout.write(text)
    except UnicodeEncodeError:
        stdout.buffer.write(text.encode(stdout.encoding, 'backslashreplace'))
    stdout.write('\n')
    ns_set(instance_dict(builtins, True), '_', object)


def breakpointhook(*args, **kws):
    """This hook function is called by built-in breakpoint()."""
    hookname = _getenv('PYTHONBREAKPOINT') if _config['use_environment'] else None
    if hookname is None or len(hookname) == 0:
        hookname = 'pdb.set_trace'
    elif hookname == '0':
        return None
    modname, dot, attr = hookname.rpartition('.')
    if not dot:
        modname, attr = 'builtins', hookname
    try:
        hook = getattr(__import__(modname, fromlist=[attr]), attr)
    except (ImportError, AttributeError):
        __import__('warnings').warn('Ignoring unimportable $PYTHONBREAKPOINT: "%s"' % hookname,
                                    RuntimeWarning)
        return None
    return hook(*args, **kws)


def unraisablehook(unraisable, /):
    """Handle an unraisable exception.

The unraisable argument has the following attributes:

* exc_type: Exception type.
* exc_value: Exception value, can be None.
* exc_traceback: Exception traceback, can be None.
* err_msg: Error message, can be None.
* object: Object causing the exception, can be None."""
    message = unraisable.err_msg
    if message is None:
        message = 'Exception ignored in'
    exc = unraisable.exc_value
    if exc is None:
        exc = unraisable.exc_type()
    if exc.__traceback__ is not unraisable.exc_traceback:
        exc.__traceback__ = unraisable.exc_traceback
    write_unraisable(message, unraisable.object, exc)


def getsizeof(object, default=MISSING, /):
    """getsizeof(object [, default]) -> int

Return the size of object in bytes."""
    try:
        method = type(object).__sizeof__
    except AttributeError:
        if default is not MISSING:
            return default
        raise TypeError("Type %s doesn't define __sizeof__" % type(object).__name__) from None
    try:
        size = method(object)
        size = size.__index__() if not isinstance(size, int) else size
    except TypeError:
        if default is not MISSING:
            return default
        raise
    if size < 0:
        raise ValueError('__sizeof__() should return >= 0')
    return size


_origin_depth = 0
_dlopen_flags = 2
_switch_interval = 0.005


def get_coroutine_origin_tracking_depth():
    """Check status of origin tracking for coroutine objects in this thread."""
    return _origin_depth


def set_coroutine_origin_tracking_depth(depth):
    """Enable or disable origin tracking for coroutine objects in this thread."""
    global _origin_depth
    if depth < 0:
        raise ValueError('depth must be >= 0')
    _origin_depth = depth


def setswitchinterval(interval, /):
    """Set the ideal thread switch delay inside the Python interpreter."""
    global _switch_interval
    if interval <= 0.0:
        raise ValueError('switch interval must be strictly positive')
    _switch_interval = interval


def getdlopenflags():
    """Return the current value of the flags that are used for dlopen calls."""
    return _dlopen_flags


def setdlopenflags(flags, /):
    """Set the flags used by the interpreter for dlopen calls."""
    global _dlopen_flags
    _dlopen_flags = flags


def getallocatedblocks():
    """Return the number of memory blocks currently allocated."""
    return 0


def getunicodeinternedsize(*, _only_immortal=False):
    """Return the number of elements of the unicode interned dictionary"""
    return 0


def _is_interned(string, /):
    """Return True if the given string is "interned"."""
    return True


def _is_immortal(op, /):
    """Return True if the given object is "immortal" per PEP 683."""
    return False


def _clear_type_cache():
    """Clear the internal type lookup cache."""


def _clear_internal_caches():
    """Clear all internal performance-related caches."""


def _clear_type_descriptors(type, /):
    """Clear type descriptors caches."""


def _debugmallocstats():
    """Print summary info to stderr about the state of pymalloc's structures."""


def _getframemodulename(depth=0):
    """Return the name of the module for a calling frame."""
    f = frame_at(depth)
    if f is None:
        return None
    return f.f_globals.get('__name__')


def _current_frames():
    """Return a dict mapping each thread's thread id to its current stack frame."""
    return {__import__('_thread').get_ident(): frame_at(1)}


def _current_exceptions():
    """Return a dict mapping each thread's identifier to its current raised exception."""
    return {__import__('_thread').get_ident(): exceptions.exception()}


def _get_cpu_count_config():
    """Private function for getting PyConfig.cpu_count"""
    return -1


def is_remote_debug_enabled():
    """Return True if remote debugging is enabled, False otherwise."""
    return False


def is_stack_trampoline_active():
    """Return *True* if a stack profiler trampoline is active."""
    return False


stdlib_module_names = frozenset((
    '__future__ _abc _aix_support _android_support _apple_support _ast _ast_unparse _asyncio '
    '_bisect _blake2 _bz2 _codecs _codecs_cn _codecs_hk _codecs_iso2022 _codecs_jp _codecs_kr '
    '_codecs_tw _collections _collections_abc _colorize _compat_pickle _contextvars _csv '
    '_ctypes _curses _curses_panel _datetime _dbm _decimal _elementtree _frozen_importlib '
    '_frozen_importlib_external _functools _gdbm _hashlib _heapq _hmac _imp _interpchannels '
    '_interpqueues _interpreters _io _ios_support _json _locale _lsprof _lzma _markupbase _md5 '
    '_multibytecodec _multiprocessing _opcode _opcode_metadata _operator _osx_support '
    '_overlapped _pickle _posixshmem _posixsubprocess _py_abc _py_warnings _pydatetime '
    '_pydecimal _pyio _pylong _pyrepl _queue _random _remote_debugging _scproxy _sha1 _sha2 '
    '_sha3 _signal _sitebuiltins _socket _sqlite3 _sre _ssl _stat _statistics _string _strptime '
    '_struct _suggestions _symtable _sysconfig _thread _threading_local _tkinter _tokenize '
    '_tracemalloc _types _typing _uuid _warnings _weakref _weakrefset _winapi _wmi _zoneinfo '
    '_zstd abc annotationlib antigravity argparse array ast asyncio atexit base64 bdb binascii '
    'bisect builtins bz2 cProfile calendar cmath cmd code codecs codeop collections colorsys '
    'compileall compression concurrent configparser contextlib contextvars copy copyreg csv '
    'ctypes curses dataclasses datetime dbm decimal difflib dis doctest email encodings '
    'ensurepip enum errno faulthandler fcntl filecmp fileinput fnmatch fractions ftplib '
    'functools gc genericpath getopt getpass gettext glob graphlib grp gzip hashlib heapq hmac '
    'html http idlelib imaplib importlib inspect io ipaddress itertools json keyword linecache '
    'locale logging lzma mailbox marshal math mimetypes mmap modulefinder msvcrt '
    'multiprocessing netrc nt ntpath nturl2path numbers opcode operator optparse os pathlib pdb '
    'pickle pickletools pkgutil platform plistlib poplib posix posixpath pprint profile pstats '
    'pty pwd py_compile pyclbr pydoc pydoc_data pyexpat queue quopri random re readline reprlib '
    'resource rlcompleter runpy sched secrets select selectors shelve shlex shutil signal site '
    'smtplib socket socketserver sqlite3 sre_compile sre_constants sre_parse ssl stat '
    'statistics string stringprep struct subprocess symtable sys sysconfig syslog tabnanny '
    'tarfile tempfile termios textwrap this threading time timeit tkinter token tokenize '
    'tomllib trace traceback tracemalloc tty turtle turtledemo types typing unicodedata '
    'unittest urllib uuid venv warnings wave weakref webbrowser winreg winsound wsgiref xml '
    'xmlrpc zipapp zipfile zipimport zlib zoneinfo '
).split())


_install(exit, getdefaultencoding, getfilesystemencoding, getfilesystemencodeerrors,
         getswitchinterval, _is_gil_enabled, get_asyncgen_hooks, addaudithook,
         audit, _getframe, exceptions.exception, exceptions.exc_info, generators.set_asyncgen_hooks,
         excepthook, displayhook, breakpointhook, unraisablehook, getsizeof,
         get_coroutine_origin_tracking_depth, set_coroutine_origin_tracking_depth,
         setswitchinterval, getdlopenflags, setdlopenflags, getallocatedblocks,
         getunicodeinternedsize, _is_interned, _is_immortal, _clear_type_cache,
         _clear_internal_caches, _clear_type_descriptors, _debugmallocstats, _getframemodulename,
         _current_frames, _current_exceptions, _get_cpu_count_config, is_remote_debug_enabled,
         is_stack_trampoline_active)
for _name in ('excepthook', 'displayhook', 'breakpointhook', 'unraisablehook'):
    ns_set(_dict, '__' + _name + '__', ns_get(_dict, _name, None))
ns_set(_dict, 'stdlib_module_names', stdlib_module_names)


def _no():
    return False


_jit = type('_jit', (), {})()
for _name in ('is_available', 'is_enabled', 'is_active'):
    ns_set(instance_dict(_jit, True), _name, _no)
ns_set(_dict, '_jit', _jit)


# What the PyConfig decides

_config = None


def configure(c):
    global _config
    _config = c
    put = lambda name, value: ns_set(_dict, name, value)
    """Set flags, argv, the paths and the options from the config dict."""
    flag_names = (
        'debug', 'inspect', 'interactive', 'optimize', 'dont_write_bytecode', 'no_user_site',
        'no_site', 'ignore_environment', 'verbose', 'bytes_warning', 'quiet',
        'hash_randomization', 'isolated', 'dev_mode', 'utf8_mode', 'warn_default_encoding',
        'safe_path', 'int_max_str_digits', 'gil', 'thread_inherit_context',
        'context_aware_warnings')
    flags_type = _structseq('sys.flags', 'sys.flags\n\nFlags provided through command line '
                            'arguments or environment vars.', flag_names, 18, False)
    flag_values = (
        c['parser_debug'], c['inspect'], c['interactive'], c['optimization_level'],
        int(not c['write_bytecode']), int(not c['user_site_directory']), int(not c['site_import']),
        int(not c['use_environment']), c['verbose'], c['bytes_warning'], c['quiet'],
        int(not c['use_hash_seed'] or c['hash_seed'] != 0), c['isolated'], bool(c['dev_mode']),
        c['utf8_mode'], c['warn_default_encoding'], bool(c['safe_path']), c['int_max_str_digits'],
        None if c['enable_gil'] < 0 else c['enable_gil'], c['thread_inherit_context'],
        c['context_aware_warnings'])
    put('flags', _make(flags_type, flag_values[:18], dict(zip(flag_names[18:], flag_values[18:]))))

    put('argv', c['argv'])
    put('orig_argv', c['orig_argv'])
    put('warnoptions', c['warnoptions'])
    put('dont_write_bytecode', not c['write_bytecode'])
    put('pycache_prefix', c['pycache_prefix'])
    put('executable', c['executable'])
    put('_base_executable', c['executable'])
    put('builtin_module_names', tuple(sorted(c['builtin_module_names'])))
    xoptions = {}
    for option in c['xoptions']:
        key, equals, value = option.partition('=')
        xoptions[key] = value if equals else True
    put('_xoptions', xoptions)
    path = [c['sys_path_0']] if c['sys_path_0'] is not None else []
    path.extend(entry for entry in c['pythonpath'].split(':') if entry)
    if c['stdlib_dir'] is not None:
        path.append(c['stdlib_dir'])
    put('path', path)
    for name in ('prefix', 'exec_prefix', 'base_prefix', 'base_exec_prefix'):
        put(name, c[name] if c[name] is not None else '')
    put('_stdlib_dir', c['stdlib_dir'])


# The standard streams (Python/pylifecycle.c init_sys_streams)

def _getenv(name):
    prefix = name.encode() + b'='
    for entry in os_environ():
        if entry.startswith(prefix):
            return entry[len(prefix):].decode('utf-8', 'surrogateescape')
    return None


def _stdio_encoding_and_errors():
    """config_init_stdio_encoding: PyConfig.stdio_*, else PYTHONIOENCODING,
else UTF-8 mode, else the locale. The default handler is surrogateescape in
UTF-8 mode and in the C locale, whose codeset glibc names ANSI_X3.4-1968."""
    c = _config
    encoding, errors = c['stdio_encoding'], c['stdio_errors']
    spec = _getenv('PYTHONIOENCODING') if c['use_environment'] else None
    if spec and (encoding is None or errors is None):
        name, _, handler = spec.partition(':')
        if name:
            encoding = encoding or name
            handler = handler or 'strict'
        errors = errors or handler or None
    if encoding is None:
        encoding = 'utf-8' if c['utf8_mode'] else locale_encoding()
    try:
        encoding = __import__('_codecs').lookup(encoding).name       # config_get_codec_name
    except LookupError:
        pass
    if errors is None:
        errors = 'surrogateescape' if c['utf8_mode'] or locale_encoding() == 'ANSI_X3.4-1968' else 'strict'
    return encoding, errors


def _create_stdio(io, fd, write, name, encoding, errors):
    """A TextIOWrapper over file descriptor fd, or None when fd is not open."""
    try:
        fstat(fd)
    except OSError:
        return None
    buffered = _config['buffered_stdio']
    # stdin is always buffered: TextIOWrapper wants read1().
    buffering = 0 if write and not buffered else -1
    buf = io.open(fd, 'wb' if write else 'rb', buffering, None, None, None, False)
    raw = buf.raw if buffering else buf
    raw.name = name
    stream = io.TextIOWrapper(buf, encoding, errors, '\n', bool(buffered and (raw.isatty() or fd == 2)),
                              not buffered)
    stream.mode = 'w' if write else 'r'
    return stream


def init_streams():
    """Sets sys.stdin, stdout, stderr and their __std*__ twins, and builtins.open."""
    io = __import__('_io')
    encoding, errors = _stdio_encoding_and_errors()
    for fd, write, name, handler in ((0, False, 'stdin', errors), (1, True, 'stdout', errors),
                                     (2, True, 'stderr', 'backslashreplace')):
        stream = _create_stdio(io, fd, write, '<' + name + '>', encoding, handler)
        ns_set(_dict, '__' + name + '__', stream)
        ns_set(_dict, name, stream)
    ns_set(instance_dict(__import__('builtins'), True), 'open', io.open)
