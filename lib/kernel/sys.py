"""The sys module (Python/sysmodule.c): its values and functions, then
configure(config), which Py_InitializeFromConfig calls with what the
PyConfig says (runtime/interp/sysmodule.c builds the dict).

sys itself is made natively before the kernel, with modules, the recursion
limit and int digits; this module adds the rest. Functions are installed as
builtins of sys, as C functions are, so their frames never show.
"""

from _lucifer import ns_set, ns_get, instance_dict, builtin_function, MISSING
from _lucifer import os_environ, os_fstat as fstat, locale_encoding
from _lucifer import exceptions, generators, structseq
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
        ('thread_info', thread_info), ('prefix', ''), ('exec_prefix', ''), ('base_prefix', ''),
        ('base_exec_prefix', ''), ('_framework', ''), ('_git', ('CPython', '', '')),
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
    _audit_hooks.append(hook)


def audit(event, /, *args):
    """Passes the event to any audit hooks that are attached."""
    for hook in _audit_hooks:
        hook(event, args)


def _getframe(depth=0, /):
    """Return a frame object from the call stack (arrives with frame objects)."""
    raise ValueError('call stack is not deep enough')


_install(exit, getdefaultencoding, getfilesystemencoding, getfilesystemencodeerrors,
         getswitchinterval, _is_gil_enabled, get_asyncgen_hooks, addaudithook,
         audit, _getframe, exceptions.exception, exceptions.exc_info, generators.set_asyncgen_hooks)


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
    put('path', path)


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
