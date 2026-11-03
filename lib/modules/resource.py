"""resource (Modules/resource.c): resource limits and usage over getrlimit,
setrlimit and getrusage."""

import _lucifer

error = OSError

RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NPROC = 0, 1, 2, 3, 4, 5, 6
RLIMIT_NOFILE, RLIMIT_MEMLOCK, RLIMIT_AS, RLIMIT_LOCKS, RLIMIT_SIGPENDING, RLIMIT_MSGQUEUE = 7, 8, 9, 10, 11, 12
RLIMIT_NICE, RLIMIT_RTPRIO, RLIMIT_RTTIME = 13, 14, 15
RLIMIT_OFILE = RLIMIT_NOFILE
RLIM_INFINITY = -1
RUSAGE_SELF, RUSAGE_CHILDREN, RUSAGE_THREAD = 0, -1, 1

struct_rusage = _lucifer.structseq.structseq_type(
    'resource.struct_rusage', 'struct_rusage: Result from getrusage.\n\n'
    'This object may be accessed either as a tuple of\n'
    '    (utime,stime,maxrss,ixrss,idrss,isrss,minflt,majflt,\n'
    '    nswap,inblock,oublock,msgsnd,msgrcv,nsignals,nvcsw,nivcsw)\n'
    'or via the attributes ru_utime, ru_stime, ru_maxrss, and so on.',
    ('ru_utime', 'ru_stime', 'ru_maxrss', 'ru_ixrss', 'ru_idrss', 'ru_isrss', 'ru_minflt', 'ru_majflt',
     'ru_nswap', 'ru_inblock', 'ru_oublock', 'ru_msgsnd', 'ru_msgrcv', 'ru_nsignals', 'ru_nvcsw',
     'ru_nivcsw'), 16)


def getrlimit(resource, /):
    """Return the soft and hard limits of a resource."""
    if not 0 <= resource < 16:
        raise ValueError('invalid resource specified')
    return _lucifer.os_getrlimit(resource)


def setrlimit(resource, limits, /):
    """Set the soft and hard limits of a resource."""
    if not 0 <= resource < 16:
        raise ValueError('invalid resource specified')
    soft, hard = limits
    try:
        _lucifer.os_setrlimit(resource, soft, hard)
    except PermissionError:
        raise ValueError('not allowed to raise maximum limit') from None


def getrusage(who, /):
    """Return resource usage statistics for the current process or its children."""
    return _lucifer.structseq.structseq_make(struct_rusage, _lucifer.os_getrusage(who), {})


def getpagesize():
    """Return the number of bytes in a memory page."""
    return 4096
