"""_thread (Modules/_threadmodule.c): locks, thread ids and thread-local
data. A lock is a PyMutex kept in the byte of a bytearray, which the
_lucifer.mutex_* primitives lock and unlock. Starting a thread arrives with
the scheduler; until then the process has the one thread.
"""

import _lucifer

TIMEOUT_MAX = 9223372036.0
_NAME_MAXLEN = 15
error = RuntimeError


def get_ident():
    """Return a non-zero integer that uniquely identifies the current thread
amongst other threads that exist simultaneously."""
    return _lucifer.thread_ident()


def get_native_id():
    """Return a non-negative integer identifying the thread as reported
by the OS (kernel)."""
    return _lucifer.thread_native_id()


def _timeout_ns(blocking, timeout):
    """Validates acquire's arguments: the wait in nanoseconds, -1 for none."""
    if not blocking:
        if timeout != -1:
            raise ValueError("can't specify a timeout for a non-blocking call")
        return 0
    if timeout < 0 and timeout != -1:
        raise ValueError('timeout value must be a non-negative number')
    if timeout == -1:
        return -1
    if timeout > TIMEOUT_MAX:
        raise OverflowError('timestamp out of range for C PyTime_t')
    return round(timeout * 1e9)


class lock:
    """A lock object is a synchronization primitive.  To create a lock,
call threading.Lock().  Methods are:

acquire() -- lock the lock, possibly blocking until it can be obtained
release() -- unlock of the lock
locked() -- test whether the lock is currently locked

A lock is not owned by the thread that locked it; another thread may
unlock it.  A thread attempting to lock a lock that it has already locked
will block until another thread unlocks it.  Deadlocks may ensue."""

    __slots__ = ('_mutex',)

    def __init__(self):
        self._mutex = _lucifer.mutex_new()

    def acquire(self, blocking=True, timeout=-1):
        """Lock the lock.  Without argument, this blocks if the lock is already
locked (even by the same thread), waiting for another thread to release
the lock, and return True once the lock is acquired.
With an argument, this will only block if the argument is true,
and the return value reflects whether the lock is acquired.
The blocking operation is interruptible."""
        return _lucifer.mutex_acquire(self._mutex, _timeout_ns(blocking, timeout))

    __enter__ = acquire

    def release(self):
        """Release the lock, allowing another thread that is blocked waiting for
the lock to acquire the lock.  The lock must be in the locked state,
but it needn't be locked by the same thread that unlocks it."""
        if not _lucifer.mutex_release(self._mutex):
            raise RuntimeError('release unlocked lock')

    def __exit__(self, exc_type, exc_value, traceback):
        self.release()

    def locked(self):
        """Return whether the lock is in the locked state."""
        return _lucifer.mutex_locked(self._mutex)

    acquire_lock = acquire
    release_lock = release
    locked_lock = locked

    def _at_fork_reinit(self):
        self._mutex = _lucifer.mutex_new()

    def __repr__(self):
        return '<%s %s.%s object at %#x>' % (
            'locked' if self.locked() else 'unlocked', type(self).__module__,
            type(self).__qualname__, id(self))


LockType = lock


def allocate_lock():
    """Create a new lock object. See help(type(threading.Lock())) for
information about locks."""
    return lock()


allocate = allocate_lock


class RLock:
    """Lock that can be acquired multiple times by the same thread."""

    __slots__ = ('_block', '_owner', '_count')

    def __init__(self):
        self._block = lock()
        self._owner = 0
        self._count = 0

    def acquire(self, blocking=True, timeout=-1):
        """Lock the lock.  `blocking` indicates whether we should wait
for the lock to be available or not.  If `blocking` is False
and another thread holds the lock, the method will return False
immediately.  If `blocking` is True and another thread holds
the lock, the method will wait for the lock to be released,
take it and then return True.
(note: the blocking operation is interruptible.)

In all other cases, the method will return True immediately.
Precisely, if the current thread already holds the lock, its
internal counter is simply incremented. If nobody holds the lock,
the lock is taken and its internal counter initialized to 1."""
        me = get_ident()
        if self._count and self._owner == me:
            self._count += 1
            return True
        if self._block.acquire(blocking, timeout):
            self._owner = me
            self._count = 1
            return True
        return False

    __enter__ = acquire

    def release(self):
        """Release the lock, allowing another thread that is blocked waiting for
the lock to acquire the lock.  The lock must be in the locked state,
and must be locked by the same thread that unlocks it; otherwise a
`RuntimeError` is raised.

Do note that if the lock was acquire()d several times in a row by the
current thread, release() needs to be called as many times for the lock
to be available for other threads."""
        if not self._count or self._owner != get_ident():
            raise RuntimeError('cannot release un-acquired lock')
        self._count -= 1
        if not self._count:
            self._owner = 0
            self._block.release()

    def __exit__(self, exc_type, exc_value, traceback):
        self.release()

    def locked(self):
        """Return a boolean indicating whether this object is locked right now."""
        return self._block.locked()

    def _is_owned(self):
        return bool(self._count) and self._owner == get_ident()

    def _recursion_count(self):
        return self._count if self._owner == get_ident() else 0

    def _release_save(self):
        if not self._count:
            raise RuntimeError('cannot release un-acquired lock')
        state = (self._count, self._owner)
        self._count = 0
        self._owner = 0
        self._block.release()
        return state

    def _acquire_restore(self, state):
        self._block.acquire()
        self._count, self._owner = state

    def _at_fork_reinit(self):
        self._block._at_fork_reinit()
        self._owner = 0
        self._count = 0

    def __repr__(self):
        return '<%s %s.%s object owner=%d count=%d at %#x>' % (
            'locked' if self._count else 'unlocked', type(self).__module__,
            type(self).__qualname__, self._owner, self._count, id(self))


class _local:
    """Thread-local data. Until threads can start, every thread's data is the one
object's attributes."""


def start_new_thread(function, args, kwargs=None, /):
    """Start a new thread and return its identifier."""
    raise RuntimeError("can't start new thread")


start_new = start_new_thread


def start_joinable_thread(function, handle=None, daemon=True):
    """*For internal use only*: start a new thread."""
    raise RuntimeError("can't start new thread")


def _count():
    """Return the number of currently running Python threads, excluding
the main thread."""
    return 0


def daemon_threads_allowed():
    """Return True if daemon threads are allowed in the current interpreter,
and False otherwise."""
    return True


def _is_main_interpreter():
    """Return True if the current interpreter is the main Python interpreter."""
    return True


def _get_main_thread_ident():
    """Internal only. Return a non-zero integer that uniquely identifies the main thread
of the main interpreter."""
    return get_ident()


def stack_size(size=0, /):
    """Return the thread stack size used when creating new threads.  The
optional size argument specifies the stack size (in bytes) to be used
for subsequently created threads, and must be 0 (use platform or
configured default) or a positive integer value of at least 32,768 (32k)."""
    return 0


def exit():
    """This is synonymous to ``raise SystemExit''.  It will cause the current
thread to exit silently unless the exception is caught."""
    raise SystemExit


exit_thread = exit


def interrupt_main(signum=2, /):
    """Simulate the arrival of the given signal in the main thread,
where the corresponding signal handler will be executed."""
    _lucifer.signal_interrupt(signum)
