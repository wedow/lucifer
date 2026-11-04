"""_queue (Modules/_queuemodule.c): SimpleQueue, an unbounded FIFO queue whose
get blocks on a lock of its own per waiter, so that every wait is one lu_park."""

from _collections import deque
from _thread import allocate_lock
from types import GenericAlias


class Empty(Exception):
    """Exception raised by Queue.get(block=0)/get_nowait()."""


class SimpleQueue:
    """Simple, unbounded, reentrant FIFO queue."""

    __slots__ = ('_items', '_waiters', '_lock', '__weakref__')

    def __init__(self):
        self._items = deque()
        self._waiters = deque()         # the locks of the getters waiting, oldest first
        self._lock = allocate_lock()

    def put(self, item, block=True, timeout=None):
        """Put the item on the queue.

The optional 'block' and 'timeout' arguments are ignored, as this method
never blocks.  They are provided for compatibility with the Queue class."""
        with self._lock:
            self._items.append(item)
            if self._waiters:
                self._waiters.popleft().release()

    def put_nowait(self, item):
        """Put an item into the queue without blocking."""
        self.put(item)

    def get(self, block=True, timeout=None):
        """Remove and return an item from the queue.

If optional args 'block' is true and 'timeout' is None (the default),
block if necessary until an item is available. If 'timeout' is
a non-negative number, it blocks at most 'timeout' seconds and raises
the Empty exception if no item was available within that time.
Otherwise ('block' is false), return an item if one is immediately
available, else raise the Empty exception ('timeout' is ignored
in that case)."""
        deadline = None
        if block and timeout is not None:
            if timeout < 0:
                raise ValueError("'timeout' must be a non-negative number")
            import time
            deadline = time.monotonic() + timeout
        while True:
            with self._lock:
                if self._items:
                    return self._items.popleft()
                if not block:
                    raise Empty
                waiter = allocate_lock()
                waiter.acquire()
                self._waiters.append(waiter)
            try:
                if deadline is None:
                    woken = waiter.acquire()
                else:
                    woken = waiter.acquire(True, max(0.0, deadline - time.monotonic()))
            except BaseException:
                # A signal handler raised: the wait leaves, and a put's wake meant for it goes on.
                with self._lock:
                    if waiter in self._waiters:
                        self._waiters.remove(waiter)
                    elif self._waiters:
                        self._waiters.popleft().release()
                raise
            if not woken:
                with self._lock:
                    try:
                        self._waiters.remove(waiter)
                    except ValueError:      # a put had taken it off the list: its item is ours to look for
                        continue
                raise Empty

    def get_nowait(self):
        """Remove and return an item from the queue without blocking."""
        return self.get(False)

    def empty(self):
        """Return True if the queue is empty, False otherwise (not reliable!)."""
        return not self._items

    def qsize(self):
        """Return the approximate size of the queue (not reliable!)."""
        return len(self._items)

    __class_getitem__ = classmethod(GenericAlias)


SimpleQueue.__module__ = '_queue'
Empty.__module__ = '_queue'
