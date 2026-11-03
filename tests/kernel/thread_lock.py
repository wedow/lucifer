import _thread

lock = _thread.allocate_lock()
print(type(lock).__name__, type(lock).__module__, _thread.LockType is type(lock), lock.locked())
print(lock.acquire(), lock.locked(), lock.acquire(False), lock.acquire(True, 0), lock.acquire(timeout=0.01))
lock.release()
print(lock.locked())
with lock:
    print(lock.locked())
print(lock.locked())
for call in (lambda: lock.release(), lambda: lock.acquire(False, 5), lambda: lock.acquire(True, -2),
             lambda: lock.acquire(True, 1e100)):
    try:
        call()
    except (RuntimeError, ValueError, OverflowError) as e:
        print(type(e).__name__, e)

rlock = _thread.RLock()
print(type(rlock).__name__, rlock.acquire(), rlock.acquire(), rlock._recursion_count(), rlock._is_owned())
rlock.release()
rlock.release()
print(rlock._is_owned(), rlock.locked())
try:
    rlock.release()
except RuntimeError as e:
    print(e)
with rlock:
    with rlock:
        print(rlock._recursion_count())
state = None
rlock.acquire()
rlock.acquire()
state = rlock._release_save()
print(state[0], rlock.locked())
rlock._acquire_restore(state)
print(rlock._recursion_count())

print(type(_thread.get_ident()).__name__, _thread.get_ident() == _thread.get_ident(), _thread.get_ident() > 0)
print(type(_thread.get_native_id()).__name__, _thread.get_native_id() > 0)
print(_thread.TIMEOUT_MAX, _thread.error is RuntimeError, _thread._count(), _thread.daemon_threads_allowed())
local = _thread._local()
local.x = 3
print(local.x)
