"""posix (Modules/posixmodule.c): the operating system calls the bootstrap and
the file layer need, over the _lucifer.os_* primitives, which make each
call inside the syscall bracket."""

import _lucifer

error = OSError

F_OK = 0
X_OK = 1
W_OK = 2
R_OK = 4
O_RDONLY = 0
O_WRONLY = 1
O_RDWR = 2
O_ACCMODE = 3
O_CREAT = 64
O_EXCL = 128
O_NOCTTY = 256
O_TRUNC = 512
O_APPEND = 1024
O_NONBLOCK = 2048
O_NDELAY = O_NONBLOCK
O_DSYNC = 4096
O_ASYNC = 8192
O_DIRECT = 16384
O_LARGEFILE = 0
O_DIRECTORY = 65536
O_NOFOLLOW = 131072
O_NOATIME = 262144
O_CLOEXEC = 524288
O_SYNC = 1052672
O_PATH = 2097152
O_TMPFILE = 4259840

stat_result = _lucifer.structseq.structseq_type(
    'os.stat_result', 'stat_result: Result from stat, fstat, or lstat.\n\n'
    'This object may be accessed either as a tuple of\n'
    '  (mode, ino, dev, nlink, uid, gid, size, atime, mtime, ctime)\n'
    'or via the attributes st_mode, st_ino, st_dev, st_nlink, st_uid, and so on.\n\n'
    'Posix/windows: If your platform supports st_blksize, st_blocks, st_rdev,\n'
    'or st_flags, they are available as attributes only.\n\n'
    'See os.stat for more information.',
    ('st_mode', 'st_ino', 'st_dev', 'st_nlink', 'st_uid', 'st_gid', 'st_size', None, None, None,
     'st_atime', 'st_mtime', 'st_ctime', 'st_atime_ns', 'st_mtime_ns', 'st_ctime_ns',
     'st_blksize', 'st_blocks', 'st_rdev'), 10)
_make_stat = _lucifer.structseq.structseq_make


def _stat_result(f):
    mode, ino, dev, nlink, uid, gid, size, as_, an, ms, mn, cs, cn, blksize, blocks, rdev = f
    return _make_stat(stat_result, (mode, ino, dev, nlink, uid, gid, size, as_, ms, cs), {
        'st_atime': as_ + an * 1e-9, 'st_mtime': ms + mn * 1e-9, 'st_ctime': cs + cn * 1e-9,
        'st_atime_ns': as_ * 1000000000 + an, 'st_mtime_ns': ms * 1000000000 + mn,
        'st_ctime_ns': cs * 1000000000 + cn, 'st_blksize': blksize, 'st_blocks': blocks,
        'st_rdev': rdev})


def fspath(path):
    """Return the file system path representation of the object.

If the object is str or bytes, then allow it to pass through as-is. If the
object defines __fspath__(), then return the result of that method. All other
types raise a TypeError."""
    if isinstance(path, (str, bytes)):
        return path
    try:
        method = type(path).__fspath__
    except AttributeError:
        raise TypeError('expected str, bytes or os.PathLike object, not '
                        + type(path).__name__) from None
    result = method(path)
    if isinstance(result, (str, bytes)):
        return result
    raise TypeError('expected %s.__fspath__() to return str or bytes, not %s'
                    % (type(path).__name__, type(result).__name__))


def _path(function, path, allowed='string, bytes or os.PathLike'):
    """path_converter: a str or bytes path, or the one a PathLike gives."""
    if not isinstance(path, (str, bytes)):
        if not hasattr(path, '__fspath__'):
            raise TypeError('%s: path should be %s, not %s' % (function, allowed, type(path).__name__))
        path = fspath(path)
    if ('\0' if isinstance(path, str) else b'\0') in path:
        raise ValueError('%s: embedded null character in path' % function)
    return path


def getcwd():
    """Return a unicode string representing the current working directory."""
    return _lucifer.os_getcwd()


def stat(path, *, dir_fd=None, follow_symlinks=True):
    """Perform a stat system call on the given path."""
    if dir_fd is not None:
        raise NotImplementedError('stat: dir_fd')
    if isinstance(path, int):
        return _stat_result(_lucifer.os_fstat(path))
    path = _path('stat', path, 'string, bytes, os.PathLike or integer')
    return _stat_result(_lucifer.os_stat(path, bool(follow_symlinks)))


def lstat(path, *, dir_fd=None):
    """Perform a stat system call on the given path, without following symbolic links."""
    return stat(path, dir_fd=dir_fd, follow_symlinks=False)


def fstat(fd):
    """Perform a stat system call on the given file descriptor."""
    return _stat_result(_lucifer.os_fstat(fd))


def listdir(path=None):
    """Return a list containing the names of the files in the directory."""
    return _lucifer.os_listdir('.' if path is None else _path(
        'listdir', path, 'string, bytes, os.PathLike, integer or None'))


def open(path, flags, mode=0o777, *, dir_fd=None):
    """Open a file for low level IO.  Returns a file descriptor (integer)."""
    return _lucifer.os_open(_path('open', path), flags, mode)


def mkdir(path, mode=0o777, *, dir_fd=None):
    """Create a directory."""
    _lucifer.os_mkdir(_path('mkdir', path), mode)


def rmdir(path, *, dir_fd=None):
    """Remove a directory."""
    _lucifer.os_rmdir(_path('rmdir', path))


def unlink(path, *, dir_fd=None):
    """Remove a file (same as remove())."""
    _lucifer.os_unlink(_path('unlink', path))


remove = unlink


def rename(src, dst, *, src_dir_fd=None, dst_dir_fd=None):
    """Rename a file or directory."""
    _lucifer.os_rename(_path('rename', src), _path('rename', dst))


replace = rename


def close(fd):
    """Close a file descriptor."""
    _lucifer.os_close(fd)


def read(fd, length, /):
    """Read from a file descriptor.  Returns a bytes object."""
    return _lucifer.os_read(fd, length)


def write(fd, data, /):
    """Write a bytes object to a file descriptor."""
    return _lucifer.os_write(fd, data if type(data) is bytes else bytes(data))


def lseek(fd, position, whence, /):
    """Set the position of a file descriptor.  Return the new position."""
    return _lucifer.os_lseek(fd, position, whence)


def isatty(fd, /):
    """Return True if the fd is connected to a terminal."""
    return _lucifer.os_isatty(fd)


def ftruncate(fd, length, /):
    """Truncate a file, specified by file descriptor, to a specific length."""
    _lucifer.os_ftruncate(fd, length)


def dup(fd, /):
    """Return a duplicate of a file descriptor."""
    return _lucifer.os_dup(fd)


def dup2(fd, fd2, inheritable=True):
    """Duplicate file descriptor."""
    return _lucifer.os_dup(fd, fd2)


def strerror(code, /):
    """Translate an error code to a message string."""
    return _lucifer.os_strerror(code)


def cpu_count():
    """Return the number of logical CPUs in the system, or None."""
    return _lucifer.os_cpu_count()


def getpid():
    """Return the current process id."""
    return _lucifer.os_getpid()


def _exit(status):
    """Exit to the system with specified status, without normal exit processing."""
    _lucifer.os_exit(status)


def _create_environ():
    environ = {}
    for entry in _lucifer.os_environ():
        name, _, value = entry.partition(b'=')
        if name:
            environ[name] = value
    return environ


environ = _create_environ()


# No optional function is advertised: os builds empty supports_* sets from this.
_have_functions = []


# Everything below runs over os_call and the other os_* primitives.

WNOHANG = 1
WUNTRACED = 2
EX_OK = 0
SCHED_OTHER = 0
_call = _lucifer.os_call


def _no_dir_fd(function, dir_fd):
    if dir_fd is not None:
        raise NotImplementedError(function + ': dir_fd')


def chdir(path):
    """Change the current working directory to the specified path."""
    _call('chdir', _path('chdir', path))


def mkdir(path, mode=0o777, *, dir_fd=None):
    """Create a directory."""
    _no_dir_fd('mkdir', dir_fd)
    _call('mkdir', _path('mkdir', path), mode)


def rmdir(path, *, dir_fd=None):
    """Remove a directory."""
    _no_dir_fd('rmdir', dir_fd)
    _call('rmdir', _path('rmdir', path))


def unlink(path, *, dir_fd=None):
    """Remove a file (same as remove())."""
    _no_dir_fd('unlink', dir_fd)
    _call('unlink', _path('unlink', path))


remove = unlink


def rename(src, dst, *, src_dir_fd=None, dst_dir_fd=None):
    """Rename a file or directory."""
    _no_dir_fd('rename', src_dir_fd or dst_dir_fd)
    _call('rename', _path('rename', src), _path('rename', dst))


replace = rename


def link(src, dst, *, src_dir_fd=None, dst_dir_fd=None, follow_symlinks=True):
    """Create a hard link to a file."""
    _no_dir_fd('link', src_dir_fd or dst_dir_fd)
    _call('link', _path('link', src), _path('link', dst))


def symlink(src, dst, target_is_directory=False, *, dir_fd=None):
    """Create a symbolic link pointing to src named dst."""
    _no_dir_fd('symlink', dir_fd)
    _call('symlink', _path('symlink', src), _path('symlink', dst))


def readlink(path, *, dir_fd=None):
    """Return a string representing the path to which the symbolic link points."""
    _no_dir_fd('readlink', dir_fd)
    path = _path('readlink', path)
    target = _lucifer.os_readlink(path)
    return target.encode('utf-8', 'surrogateescape') if isinstance(path, bytes) else target


def chmod(path, mode, *, dir_fd=None, follow_symlinks=True):
    """Change the access permissions of a file."""
    _no_dir_fd('chmod', dir_fd)
    if isinstance(path, int):
        _call('fchmod', path, mode)
    else:
        _call('chmod', _path('chmod', path), mode)


def chown(path, uid, gid, *, dir_fd=None, follow_symlinks=True):
    """Change the owner and group id of path to the numeric uid and gid."""
    _no_dir_fd('chown', dir_fd)
    _call('chown' if follow_symlinks else 'lchown', _path('chown', path), uid, gid)


def access(path, mode, *, dir_fd=None, effective_ids=False, follow_symlinks=True):
    """Use the real uid/gid to test for access to a path."""
    _no_dir_fd('access', dir_fd)
    try:
        _call('access', _path('access', path), mode)
    except OSError:
        return False
    return True


def utime(path, times=None, *, ns=None, dir_fd=None, follow_symlinks=True):
    """Set the access and modified time of path."""
    _no_dir_fd('utime', dir_fd)
    if times is not None and ns is not None:
        raise ValueError('utime: you may specify either \'times\' or \'ns\' but not both')
    if times is not None:
        if not isinstance(times, tuple) or len(times) != 2:
            raise TypeError("utime: 'times' must be either a tuple of two ints or None")
        ns = tuple(int(t * 1000000000) if isinstance(t, float) else t * 1000000000 for t in times)
    if ns is None:
        # UTIME_NOW in both slots
        a = m = (0, (1 << 30) - 1)
    else:
        a, m = divmod(ns[0], 1000000000), divmod(ns[1], 1000000000)
    _call('utime', _path('utime', path), a[0], a[1], m[0], m[1], 0 if follow_symlinks else 0x100)


def truncate(path, length):
    """Truncate a file, specified by path, to a specific length."""
    if isinstance(path, int):
        return ftruncate(path, length)
    _call('truncate', _path('truncate', path), length)


def fsync(fd):
    """Force write of fd to disk."""
    _call('fsync', fd if isinstance(fd, int) else fd.fileno())


def fdatasync(fd):
    """Force write of fd to disk without forcing update of metadata."""
    _call('fdatasync', fd if isinstance(fd, int) else fd.fileno())


def umask(mask):
    """Set the current numeric umask and return the previous umask."""
    return _call('umask', mask)


def getuid():
    """Return the current process's user id."""
    return _call('getuid')


def geteuid():
    """Return the current process's effective user id."""
    return _call('geteuid')


def getgid():
    """Return the current process's group id."""
    return _call('getgid')


def getegid():
    """Return the current process's effective group id."""
    return _call('getegid')


def getppid():
    """Return the parent's process id."""
    return _call('getppid')


def getpgrp():
    """Return the current process group id."""
    return _call('getpgrp')


def kill(pid, signal, /):
    """Kill a process with a signal."""
    _call('kill', pid, signal)


def waitpid(pid, options, /):
    """Wait for completion of a given child process."""
    return _lucifer.os_waitpid(pid, options)


def WIFEXITED(status):
    return (status & 0x7f) == 0


def WEXITSTATUS(status):
    return (status >> 8) & 0xff


def WIFSIGNALED(status):
    return 0 < (status & 0x7f) < 0x7f


def WTERMSIG(status):
    return status & 0x7f


def WIFSTOPPED(status):
    return (status & 0xff) == 0x7f


def WSTOPSIG(status):
    return (status >> 8) & 0xff


def urandom(size, /):
    """Return a bytes object containing random bytes suitable for cryptographic use."""
    return _lucifer.os_urandom(size)


def pipe():
    """Create a pipe.  Returns a tuple of two file descriptors: (read_fd, write_fd)"""
    return _lucifer.os_pipe()


uname_result = _lucifer.structseq.structseq_type(
    'posix.uname_result', 'uname_result: Result from os.uname().\n\n'
    'This object may be accessed either as a tuple of\n'
    '  (sysname, nodename, release, version, machine),\n'
    'or via the attributes sysname, nodename, release, version, and machine.',
    ('sysname', 'nodename', 'release', 'version', 'machine'), 5)


def uname():
    """Return an object identifying the current operating system."""
    return _lucifer.structseq.structseq_make(uname_result, _lucifer.os_uname(), {})


def putenv(name, value, /):
    """Change or add an environment variable."""
    if b'=' in name or not name:
        raise ValueError('illegal environment variable name')
    _lucifer.os_putenv(name, value)


def unsetenv(name, /):
    """Delete an environment variable."""
    if b'=' in name or not name:
        raise ValueError('illegal environment variable name')
    _lucifer.os_putenv(name, None)


_F_GETFD, _F_SETFD, _F_GETFL, _F_SETFL, _FD_CLOEXEC = 1, 2, 3, 4, 1


def get_inheritable(fd, /):
    """Get the close-on-exec flag of the specified file descriptor."""
    return not _call('fcntl', fd, _F_GETFD, 0) & _FD_CLOEXEC


def set_inheritable(fd, inheritable, /):
    """Set the inheritable flag of the specified file descriptor."""
    flags = _call('fcntl', fd, _F_GETFD, 0)
    _call('fcntl', fd, _F_SETFD, flags & ~_FD_CLOEXEC if inheritable else flags | _FD_CLOEXEC)


def get_blocking(fd, /):
    """Get the blocking mode of the file descriptor."""
    return not _call('fcntl', fd, _F_GETFL, 0) & O_NONBLOCK


def set_blocking(fd, blocking, /):
    """Set the blocking mode of the specified file descriptor."""
    flags = _call('fcntl', fd, _F_GETFL, 0)
    _call('fcntl', fd, _F_SETFL, flags & ~O_NONBLOCK if blocking else flags | O_NONBLOCK)


def device_encoding(fd):
    """Return a string describing the encoding of a terminal's file descriptor."""
    return _lucifer.locale_encoding() if isatty(fd) else None


class DirEntry:
    """DirEntry: a directory entry, as scandir() yields it."""
    __slots__ = ('name', 'path', '_type', '_ino', '_stat', '_lstat')

    def __init__(self, name, path, type, ino):
        self.name, self.path, self._type, self._ino = name, path, type, ino
        self._stat = self._lstat = None

    def __fspath__(self):
        return self.path

    def __repr__(self):
        return '<DirEntry %r>' % (self.name,)

    def inode(self):
        return self._ino

    def stat(self, *, follow_symlinks=True):
        if follow_symlinks:
            if self._stat is None:
                self._stat = stat(self.path)
            return self._stat
        if self._lstat is None:
            self._lstat = lstat(self.path)
        return self._lstat

    def _is(self, wanted, follow_symlinks):
        if self._type == 0 or (self._type == 10 and follow_symlinks):
            try:
                mode = self.stat(follow_symlinks=follow_symlinks).st_mode
            except FileNotFoundError:
                return False
            return (mode & 0o170000) == wanted
        return self._type == {0o040000: 4, 0o100000: 8, 0o120000: 10}[wanted]

    def is_dir(self, *, follow_symlinks=True):
        return self._is(0o040000, follow_symlinks)

    def is_file(self, *, follow_symlinks=True):
        return self._is(0o100000, follow_symlinks)

    def is_symlink(self):
        return self._is(0o120000, False)

    def is_junction(self):
        return False

    __class_getitem__ = classmethod(lambda cls, item: __import__('types').GenericAlias(cls, item))


class _ScandirIterator:
    def __init__(self, entries):
        self._entries = iter(entries)

    def __iter__(self):
        return self

    def __next__(self):
        return next(self._entries)

    def close(self):
        self._entries = iter(())

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def scandir(path=None):
    """Return an iterator of DirEntry objects for given path."""
    if path is None:
        path = '.'
    path = _path('scandir', path, 'string, bytes, os.PathLike, integer or None')
    raw = path.decode('utf-8', 'surrogateescape') if isinstance(path, bytes) else path
    prefix = raw if raw.endswith('/') else raw + '/'
    return _ScandirIterator([
        DirEntry(name.encode('utf-8', 'surrogateescape') if isinstance(path, bytes) else name,
                 (prefix + name).encode('utf-8', 'surrogateescape') if isinstance(path, bytes) else prefix + name,
                 kind, ino)
        for name, kind, ino in _lucifer.os_scandir(path)])


def WCOREDUMP(status):
    return bool(status & 0x80)


def waitstatus_to_exitcode(status):
    """Convert a wait status to an exit code."""
    if not isinstance(status, int):
        status = status.__index__()
    if WIFEXITED(status):
        return WEXITSTATUS(status)
    if WIFSIGNALED(status):
        return -WTERMSIG(status)
    raise ValueError('invalid wait status: %d' % status)


def _exec_bytes(value):
    return value if isinstance(value, bytes) else fspath(value).encode('utf-8', 'surrogateescape') \
        if isinstance(fspath(value), str) else fspath(value)


def execv(path, argv, /):
    """Execute an executable path with arguments, replacing current process."""
    if not isinstance(argv, (tuple, list)):
        raise TypeError('execv() arg 2 must be a tuple or list')
    if not argv or not argv[0]:
        raise ValueError('execv() arg 2 first element cannot be empty')
    _lucifer.os_execve(_path('execv', path), tuple(_exec_bytes(a) for a in argv), None)


def execve(path, argv, env):
    """Execute an executable path with arguments, replacing current process."""
    if not isinstance(argv, (tuple, list)):
        raise TypeError('execve: argv must be a tuple or list')
    if not argv or not argv[0]:
        raise ValueError('execve: argv first element cannot be empty')
    entries = tuple(_exec_bytes(k) + b'=' + _exec_bytes(v) for k, v in env.items())
    _lucifer.os_execve(_path('execve', path), tuple(_exec_bytes(a) for a in argv), entries)
