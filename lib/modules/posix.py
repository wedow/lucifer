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


def getcwd():
    """Return a unicode string representing the current working directory."""
    return _lucifer.os_getcwd()


def stat(path, *, dir_fd=None, follow_symlinks=True):
    """Perform a stat system call on the given path."""
    if dir_fd is not None:
        raise NotImplementedError('stat: dir_fd')
    if isinstance(path, int):
        return _stat_result(_lucifer.os_fstat(path))
    return _stat_result(_lucifer.os_stat(fspath(path), bool(follow_symlinks)))


def lstat(path, *, dir_fd=None):
    """Perform a stat system call on the given path, without following symbolic links."""
    return stat(path, dir_fd=dir_fd, follow_symlinks=False)


def fstat(fd):
    """Perform a stat system call on the given file descriptor."""
    return _stat_result(_lucifer.os_fstat(fd))


def listdir(path=None):
    """Return a list containing the names of the files in the directory."""
    return _lucifer.os_listdir('.' if path is None else fspath(path))


def open(path, flags, mode=0o777, *, dir_fd=None):
    """Open a file for low level IO.  Returns a file descriptor (integer)."""
    return _lucifer.os_open(fspath(path), flags, mode)


def close(fd):
    """Close a file descriptor."""
    _lucifer.os_close(fd)


def read(fd, length, /):
    """Read from a file descriptor.  Returns a bytes object."""
    return _lucifer.os_read(fd, length)


def write(fd, data, /):
    """Write a bytes object to a file descriptor."""
    return _lucifer.os_write(fd, data if type(data) is bytes else bytes(data))


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
