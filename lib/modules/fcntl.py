"""fcntl (Modules/fcntlmodule.c): file control over the os_call table. flock and
lockf may wait for another process, so they run inside the syscall bracket."""

import _lucifer

FD_CLOEXEC = 1
F_DUPFD = 0
F_GETFD = 1
F_SETFD = 2
F_GETFL = 3
F_SETFL = 4
F_GETLK = 5
F_SETLK = 6
F_SETLKW = 7
F_DUPFD_CLOEXEC = 1030
LOCK_SH = 1
LOCK_EX = 2
LOCK_NB = 4
LOCK_UN = 8
F_RDLCK = 0
F_WRLCK = 1
F_UNLCK = 2


def _fd(fd):
    if isinstance(fd, int):
        return fd
    try:
        return fd.fileno().__index__()
    except AttributeError:
        raise TypeError('argument must be an int, or have a fileno() method.') from None


def fcntl(fd, cmd, arg=0, /):
    """Perform the operation `cmd` on file descriptor fd (integer arguments only)."""
    return _lucifer.os_call('fcntl', _fd(fd), cmd, arg)


def flock(fd, operation, /):
    """Perform the lock operation `operation` on file descriptor fd."""
    _lucifer.os_flock(_fd(fd), operation)


def lockf(fd, cmd, len=0, start=0, whence=0):
    """A wrapper around the fcntl() locking calls."""
    fd = _fd(fd)
    if cmd in (LOCK_UN, LOCK_SH, LOCK_EX, LOCK_SH | LOCK_NB, LOCK_EX | LOCK_NB):
        _lucifer.os_lockf(fd, cmd, len, start, whence)
    else:
        raise ValueError('unrecognized lockf argument')
