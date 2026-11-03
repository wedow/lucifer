# _thread: start_new_thread, joinable handles, locks, RLock, _local, stack_size, _count, excepthook.
# Every line a thread produces is stored and printed by the main thread after a join.
import _thread
import sys


def wait_done(handle):
    handle.join()
    return handle.is_done()


# start_new_thread: the arguments reach the function; the identifier is the thread's own.
box = []
finished = _thread.allocate_lock()
finished.acquire()


def run(a, b, *, c=0):
    box.append((a, b, c, _thread.get_ident()))
    finished.release()


ident = _thread.start_new_thread(run, (1, 2), {'c': 3})
finished.acquire()
print(box[0][:3], box[0][3] == ident, ident != _thread.get_ident(), isinstance(ident, int))

for bad in ((lambda: _thread.start_new_thread(1, ())), (lambda: _thread.start_new_thread(run, 1)),
            (lambda: _thread.start_new_thread(run, (), 1))):
    try:
        bad()
    except TypeError as e:
        print('TypeError', e)

# Joinable handles.
results = []


def job(n):
    results.append(n)


handles = [_thread.start_joinable_thread(lambda n=n: job(n), daemon=False) for n in range(5)]
print([wait_done(h) for h in handles], sorted(results))
print([h.ident == i for h, i in zip(handles, [h.ident for h in handles])], len({h.ident for h in handles}) <= 5)
handles[0].join()
try:
    _thread.start_joinable_thread(lambda: None, handle=handles[0])
except RuntimeError as e:
    print('RuntimeError', e)
try:
    _thread.start_joinable_thread(1)
except TypeError as e:
    print('TypeError', e)

# A thread joining itself fails; the main thread's handle can be joined once it is done.
seen = []
mine = _thread._ThreadHandle()


def self_join():
    try:
        mine.join()
    except RuntimeError as e:
        seen.append(str(e))


_thread.start_joinable_thread(self_join, handle=mine)
mine.join()
print(seen)
print(_thread._count())

# join(timeout) returns while the thread still runs.
release = _thread.allocate_lock()
release.acquire()
slow = _thread.start_joinable_thread(lambda: release.acquire(), daemon=False)
slow.join(0.01)
print(slow.is_done(), _thread._count())
release.release()
slow.join()
print(slow.is_done(), _thread._count())

# Locks.
lock = _thread.allocate_lock()
print(lock.locked(), lock.acquire(), lock.locked(), lock.acquire(False), lock.acquire(True, 0.01))
lock.release()
try:
    lock.release()
except RuntimeError as e:
    print('RuntimeError', e)
for args in ((False, 1), (True, -2)):
    try:
        lock.acquire(*args)
    except ValueError as e:
        print('ValueError', e)
try:
    lock.acquire(timeout=_thread.TIMEOUT_MAX * 2)
except OverflowError as e:
    print('OverflowError', e)
with lock:
    print(lock.locked())

# Another thread may release a lock.
lock.acquire()
_thread.start_new_thread(lock.release, ())
print(lock.acquire(timeout=10), lock.locked())
lock.release()

# A lock handed from thread to thread in turn.
order = []
a, b = _thread.allocate_lock(), _thread.allocate_lock()
a.acquire()
b.acquire()
done = _thread.allocate_lock()
done.acquire()


def first():
    a.acquire()
    order.append('first')
    b.release()


def second():
    b.acquire()
    order.append('second')
    done.release()


_thread.start_new_thread(second, ())
_thread.start_new_thread(first, ())
order.append('main')
a.release()
done.acquire()
print(order)

# RLock.
r = _thread.RLock()
print(r.acquire(), r.acquire(), r._is_owned(), r._recursion_count())
r.release()
r.release()
try:
    r.release()
except RuntimeError as e:
    print('RuntimeError', e)
got = []
r.acquire()
h = _thread.start_joinable_thread(lambda: got.append((r.acquire(False), r._is_owned(), r.acquire(timeout=0.01))), daemon=False)
h.join()
print(got)
r.acquire()
state = r._release_save()
print(state[0], r._is_owned())
r._acquire_restore(state)
print(r._is_owned())
r.release()

# _local: one dict per thread.
local = _thread._local()
local.x = 'main'
out = []


def use_local():
    out.append(hasattr(local, 'x'))
    local.x = 'child'
    out.append(local.x)
    out.append(sorted(local.__dict__))


wait_done(_thread.start_joinable_thread(use_local, daemon=False))
print(out, local.x)


class Sub(_thread._local):
    def __init__(self, tag):
        self.tag = tag
        self.hits = getattr(self, 'hits', 0) + 1


sub = Sub('t')
seen = []
wait_done(_thread.start_joinable_thread(lambda: seen.append((sub.tag, sub.hits)), daemon=False))
print(sub.tag, sub.hits, seen)
try:
    _thread._local(1)
except TypeError as e:
    print('TypeError', e)

# stack_size, _count, get_native_id.
print(_thread.stack_size(), _thread.stack_size(1 << 20), _thread.stack_size(), _thread.stack_size(0), _thread.stack_size())
for size in (1, -1):
    try:
        _thread.stack_size(size)
    except ValueError as e:
        print('ValueError', e)
natives = []
wait_done(_thread.start_joinable_thread(lambda: natives.append(_thread.get_native_id()), daemon=False))
print(natives[0] != _thread.get_native_id(), _thread.get_native_id() > 0, _thread._count())
print(_thread._get_main_thread_ident() == _thread.get_ident())
main_ident = []
wait_done(_thread.start_joinable_thread(lambda: main_ident.append(_thread._get_main_thread_ident()), daemon=False))
print(main_ident[0] == _thread.get_ident())

# _excepthook and its argument type.
print(_thread._ExceptHookArgs.__name__, _thread._ExceptHookArgs.n_fields)
try:
    _thread._excepthook(1)
except TypeError as e:
    print('TypeError', e)
args = _thread._ExceptHookArgs((SystemExit, SystemExit(), None, None))
print(_thread._excepthook(args))

# SystemExit ends a thread silently (what else a thread raises is reported on stderr, with an address).
def exits():
    raise SystemExit(3)


wait_done(_thread.start_joinable_thread(exits, daemon=False))
print(_thread._count())

# _thread.exit ends a thread early.
tail = []


def early():
    tail.append(1)
    _thread.exit()
    tail.append(2)


wait_done(_thread.start_joinable_thread(early, daemon=False))
print(tail)
