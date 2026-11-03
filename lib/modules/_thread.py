"""_thread (Modules/_threadmodule.c): threads, locks, thread ids and
thread-local data. A lock is a PyMutex kept in the byte of a bytearray, which
the _lucifer.mutex_* primitives lock and unlock. A thread is a new task on a
carrier of its own (DESIGN 5.7); _lucifer.thread_start runs a function in it.
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
        wait = _timeout_ns(blocking, timeout)
        me = get_ident()
        if self._count and self._owner == me:
            self._count += 1
            return True
        if _lucifer.mutex_acquire(self._block._mutex, wait):
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


class _localimpl:
    """What one _local object shares among its threads: the key under which each thread's
    dict is kept in that thread's own dict, the arguments __init__ gets in every thread, and
    the thread dicts that hold the key."""
    __slots__ = ('key', 'localargs', 'locallock', 'holders', '__weakref__')

    def __init__(self, args, kw):
        self.key = '_thread._local.%x' % id(self)
        self.localargs = (args, kw)
        self.locallock = RLock()
        self.holders = {}

    def get_dict(self):
        """This thread's dict of the local, or None."""
        entry = _lucifer.thread_dict().get(self.key)
        return entry[1] if entry is not None else None

    def create_dict(self):
        """Makes this thread's dict of the local."""
        import _weakref        # not at the top: importlib loads this module before imports work
        tdict = _lucifer.thread_dict()
        dct = {}
        tdict[self.key] = (_weakref.ref(self), dct)
        with self.locallock:
            self.holders[id(tdict)] = tdict
        return dct

    def forget(self, tdict):
        """The thread that owned tdict has ended."""
        with self.locallock:
            self.holders.pop(id(tdict), None)

    def clear(self):
        """The local is gone: its dict leaves every thread that has one."""
        with self.locallock:
            holders = list(self.holders.values())
            self.holders.clear()
        for tdict in holders:
            tdict.pop(self.key, None)


# A weakref callback runs only while its weakref lives.
_local_refs = set()


def _thread_exit():
    """The current thread ends: its locals' data goes with it, now, not at some collection."""
    tdict = _lucifer.thread_dict()
    for key, entry in list(tdict.items()):
        if type(key) is str and key.startswith('_thread._local.'):
            impl = entry[0]()
            if impl is not None:
                impl.forget(tdict)
            entry[1].clear()        # a local's __dict__ may still be this dict
    tdict.clear()


class _local:
    """Thread-local data."""
    __slots__ = ('_local__impl', '__dict__', '__weakref__')

    def __new__(cls, /, *args, **kw):
        if (args or kw) and (cls.__init__ is object.__init__):
            raise TypeError('Initialization arguments are not supported')
        self = object.__new__(cls)
        impl = _localimpl(args, kw)
        object.__setattr__(self, '_local__impl', impl)

        def die(ref):
            _local_refs.discard(ref)
            impl.clear()
        import _weakref
        ref = _weakref.ref(self, die)
        _local_refs.add(ref)
        # The creating thread's dict exists before __init__ runs, so nothing calls __init__ twice.
        object.__setattr__(self, '__dict__', impl.create_dict())
        return self

    def _enter(self):
        """Makes self.__dict__ this thread's, and returns the impl with its lock held."""
        impl = object.__getattribute__(self, '_local__impl')
        dct = impl.get_dict()
        if dct is None:
            dct = impl.create_dict()
            args, kw = impl.localargs
            object.__setattr__(self, '__dict__', dct)
            self.__init__(*args, **kw)
        impl.locallock.acquire()
        object.__setattr__(self, '__dict__', dct)
        return impl

    def __getattribute__(self, name):
        impl = _local._enter(self)
        try:
            return object.__getattribute__(self, name)
        finally:
            impl.locallock.release()

    def __setattr__(self, name, value):
        if name == '__dict__':
            raise AttributeError("%r object attribute '__dict__' is read-only" % self.__class__.__name__)
        impl = _local._enter(self)
        try:
            return object.__setattr__(self, name, value)
        finally:
            impl.locallock.release()

    def __delattr__(self, name):
        if name == '__dict__':
            raise AttributeError("%r object attribute '__dict__' is read-only" % self.__class__.__name__)
        impl = _local._enter(self)
        try:
            return object.__delattr__(self, name)
        finally:
            impl.locallock.release()


# Threads. _state guards the count of running threads, the handles of the non-daemon
# threads that _shutdown joins, and the start of a handle.
_state = lock()
_running = 0
_non_daemon = []
_stack_size = 0
_STACK_MIN = 53248              # _PyOS_MIN_STACK_SIZE + SYSTEM_PAGE_SIZE


def _run(function, args, kwargs, handle):
    """The body of a thread: calls function, reports what it raises, and marks the handle done."""
    global _running
    if handle is not None:
        handle._ident = get_ident()
    try:
        function(*args, **(kwargs or {}))
    except SystemExit:
        pass
    except BaseException as exc:
        _lucifer.write_unraisable('Exception ignored in thread started by', function, exc)
    finally:
        _thread_exit()
        with _state:
            _running -= 1
            if handle is not None and handle in _non_daemon:
                _non_daemon.remove(handle)
        if handle is not None:
            handle._set_done()


def _spawn(function, args, kwargs, handle):
    """Starts the thread and returns its identifier. The caller has counted it in _running."""
    global _running
    try:
        import _signal      # the first thread starts from the main thread: SIGINT's default handler goes in

        return _lucifer.thread_start(lambda: _run(function, args, kwargs, handle), _stack_size)
    except BaseException:
        with _state:
            _running -= 1
            if handle is not None and handle in _non_daemon:
                _non_daemon.remove(handle)
        if handle is not None:
            handle._set_done()
        raise


def start_new_thread(function, args, kwargs=None, /):
    """Start a new thread and return its identifier.

The thread will call the function with positional arguments from the tuple
args and keyword arguments taken from the optional dictionary kwargs.  The
thread exits when the function returns; the return value is ignored.  The
thread will also exit when the function raises an unhandled exception; a
stack trace will be printed unless the exception is SystemExit."""
    global _running
    if not callable(function):
        raise TypeError('first arg must be callable')
    if not isinstance(args, tuple):
        raise TypeError('2nd arg must be a tuple')
    if kwargs is not None and not isinstance(kwargs, dict):
        raise TypeError('optional 3rd arg must be a dictionary')
    with _state:
        _running += 1
    return _spawn(function, args, kwargs, None)


start_new = start_new_thread


def start_joinable_thread(function, handle=None, daemon=True):
    """*For internal use only*: start a new thread.

Like start_new_thread(), except for the following differences:
The first argument is the function to call, with no arguments.  A handle is
returned that can be used to wait for the thread to finish."""
    global _running
    if not callable(function):
        raise TypeError('thread function must be callable')
    if handle is None:
        handle = _ThreadHandle()
    with _state:
        if handle._started:
            raise RuntimeError('thread already started')
        handle._started = True
        _lucifer.mutex_acquire(handle._done, 0)
        if not daemon:
            _non_daemon.append(handle)
        _running += 1
    handle._ident = _spawn(function, (), None, handle)
    return handle


def _count():
    """Return the number of currently running Python threads, excluding
the main thread."""
    return _running


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
    return _lucifer.main_thread_ident()


def stack_size(size=0, /):
    """Return the thread stack size used when creating new threads.  The
optional size argument specifies the stack size (in bytes) to be used
for subsequently created threads, and must be 0 (use platform or
configured default) or a positive integer value of at least 32,768 (32k).
If changing the thread stack size is unsupported, a ThreadError
exception is raised.  If the specified size is invalid, a ValueError
exception is raised, and the stack size is unmodified.  32k bytes
 currently the minimum supported stack size value to guarantee
sufficient stack space for the interpreter itself.

Note that some platforms may have particular restrictions on values for
the stack size, such as requiring a minimum stack size larger than 32 KiB or
requiring allocation in multiples of the system memory page size
- platform documentation should be referred to for more information
(4 KiB pages are common; using multiples of 4096 for the stack size is
the suggested approach in the absence of more specific information)."""
    global _stack_size
    size = size.__index__()
    if size != 0 and size < _STACK_MIN:
        raise ValueError('size must be at least %d bytes' % _STACK_MIN)
    old = _stack_size
    _stack_size = size
    return old


def exit():
    """This is synonymous to ``raise SystemExit''.  It will cause the current
thread to exit silently unless the exception is caught."""
    raise SystemExit


exit_thread = exit


def interrupt_main(signum=2, /):
    """Simulate the arrival of the given signal in the main thread,
where the corresponding signal handler will be executed."""
    _lucifer.signal_interrupt(signum)


class _ThreadHandle:
    """Handle of a thread: its identifier, and a lock held from the thread's start until it ends."""
    __slots__ = ('_ident', '_done', '_started', '_exited')

    def __init__(self, ident=0, started=False):
        self._ident = ident
        self._done = _lucifer.mutex_new()
        self._started = started
        self._exited = False

    @property
    def ident(self):
        return self._ident

    def is_done(self):
        return self._exited         # not the lock's state: a join holds the lock for a moment

    def _set_done(self):
        self._exited = True
        _lucifer.mutex_release(self._done)

    def join(self, timeout=None):
        """Wait for the thread to finish."""
        if not self._started:
            raise RuntimeError('thread not started')
        if self._ident == get_ident() and not self.is_done():
            raise RuntimeError('Cannot join current thread')
        if timeout is None:
            wait = -1
        else:
            wait = max(_timeout_ns(True, max(timeout, 0)), 0)
        if _lucifer.mutex_acquire(self._done, wait):
            _lucifer.mutex_release(self._done)         # the next join finds it done

    def __repr__(self):
        return '<_thread._ThreadHandle object: ident=%d>' % self._ident


def _make_thread_handle(ident):
    """Internal only. Make a thread handle for threads not spawned by the _thread module."""
    handle = _ThreadHandle(ident, True)
    _lucifer.mutex_acquire(handle._done, 0)
    return handle


def _shutdown():
    """Wait for all non-daemon threads (other than the calling thread) to stop."""
    while True:
        with _state:
            handle = _non_daemon[-1] if _non_daemon else None
        if handle is None:
            return
        handle.join()


def set_name(name):
    """Set the name of the current thread."""


_ExceptHookArgs = _lucifer.structseq.structseq_type(
    '_thread._ExceptHookArgs', 'ExceptHookArgs\n\nType used to pass arguments to threading.excepthook.',
    ('exc_type', 'exc_value', 'exc_traceback', 'thread'), 4)
_ExceptHookArgs.__module__ = '_thread'
_ExceptHookArgs.__name__ = '_ExceptHookArgs'


def _excepthook(args, /):
    """Handle uncaught Thread.run() exception."""
    import sys
    if type(args) is not _ExceptHookArgs:
        raise TypeError('_thread.excepthook argument type must be ExceptHookArgs')
    exc_type, exc_value, exc_tb, thread = args
    if exc_type is SystemExit:
        return None
    file = sys.stderr
    if file is None:
        if thread is None:
            return None
        file = thread._stderr
        if file is None:
            return None
    name = thread.name if thread is not None else None
    file.write('Exception in thread %s:\n' % (name if name is not None else get_ident()))
    if exc_value is not None and exc_value.__traceback__ is not exc_tb:
        exc_value.__traceback__ = exc_tb
    _lucifer.err_display(exc_value)
    file.flush()
    return None
