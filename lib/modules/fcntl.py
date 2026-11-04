"""fcntl (Modules/fcntlmodule.c): file descriptor control over fcntl(2) and flock(2)."""

import _lucifer

DN_ACCESS = 1
DN_ATTRIB = 32
DN_CREATE = 4
DN_DELETE = 8
DN_MODIFY = 2
DN_MULTISHOT = 2147483648
DN_RENAME = 16
FASYNC = 8192
FD_CLOEXEC = 1
FICLONE = 1074041865
FICLONERANGE = 1075876877
F_ADD_SEALS = 1033
F_DUPFD = 0
F_DUPFD_CLOEXEC = 1030
F_DUPFD_QUERY = 1027
F_EXLCK = 4
F_GETFD = 1
F_GETFL = 3
F_GETLEASE = 1025
F_GETLK = 5
F_GETLK64 = 5
F_GETOWN = 9
F_GETOWN_EX = 16
F_GETPIPE_SZ = 1032
F_GETSIG = 11
F_GET_FILE_RW_HINT = 1037
F_GET_RW_HINT = 1035
F_GET_SEALS = 1034
F_NOTIFY = 1026
F_OFD_GETLK = 36
F_OFD_SETLK = 37
F_OFD_SETLKW = 38
F_OWNER_PGRP = 2
F_OWNER_PID = 1
F_OWNER_TID = 0
F_RDLCK = 0
F_SEAL_FUTURE_WRITE = 16
F_SEAL_GROW = 4
F_SEAL_SEAL = 1
F_SEAL_SHRINK = 2
F_SEAL_WRITE = 8
F_SETFD = 2
F_SETFL = 4
F_SETLEASE = 1024
F_SETLK = 6
F_SETLK64 = 6
F_SETLKW = 7
F_SETLKW64 = 7
F_SETOWN = 8
F_SETOWN_EX = 15
F_SETPIPE_SZ = 1031
F_SETSIG = 10
F_SET_FILE_RW_HINT = 1038
F_SET_RW_HINT = 1036
F_SHLCK = 8
F_UNLCK = 2
F_WRLCK = 1
LOCK_EX = 2
LOCK_MAND = 32
LOCK_NB = 4
LOCK_READ = 64
LOCK_RW = 192
LOCK_SH = 1
LOCK_UN = 8
LOCK_WRITE = 128
RWH_WRITE_LIFE_EXTREME = 5
RWH_WRITE_LIFE_LONG = 4
RWH_WRITE_LIFE_MEDIUM = 3
RWH_WRITE_LIFE_NONE = 1
RWH_WRITE_LIFE_NOT_SET = 0
RWH_WRITE_LIFE_SHORT = 2

def _fd(fd):
    if isinstance(fd, int):
        return fd
    if hasattr(fd, 'fileno'):
        fd = fd.fileno()
        if isinstance(fd, int):
            return fd
    raise TypeError('argument must be an int, or have a fileno() method.')


def fcntl(fd, cmd, arg=0, /):
    """Perform the operation `cmd` on file descriptor fd (an integer argument only)."""
    if not isinstance(arg, int):
        raise NotImplementedError('fcntl with a buffer argument')
    return _lucifer.os_call('fcntl', _fd(fd), cmd, arg)


def flock(fd, operation, /):
    """Perform the lock operation `operation` on file descriptor fd."""
    _lucifer.os_call('flock', _fd(fd), operation)


def lockf(fd, cmd, len=0, start=0, whence=0):
    """A wrapper around the fcntl() locking calls (whole-file and record locks need a struct flock)."""
    fd = _fd(fd)
    if cmd in (LOCK_UN, LOCK_SH, LOCK_EX, LOCK_UN | LOCK_NB, LOCK_SH | LOCK_NB, LOCK_EX | LOCK_NB) and not len and not start:
        _lucifer.os_call('flock', fd, cmd)
        return
    raise NotImplementedError('lockf on a byte range')


def ioctl(fd, request, arg=0, mutate_flag=True, /):
    """Perform the operation `request` on file descriptor fd."""
    raise NotImplementedError('ioctl')
