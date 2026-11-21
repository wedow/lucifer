import _imp
import _thread
import os


def release_from_another_thread():
    try:
        _imp.release_lock()
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))
    print("held in the other thread", _imp.lock_held())


_imp.acquire_lock()
_imp.acquire_lock()

# The child keeps the owner's two acquisitions across the fork.
pid = os.fork()
if pid == 0:
    _imp.release_lock()
    assert _imp.lock_held()
    _imp.release_lock()
    os._exit(int(_imp.lock_held()))
print("child status", os.waitstatus_to_exitcode(os.waitpid(pid, 0)[1]))

_thread.start_joinable_thread(release_from_another_thread).join()

for _ in range(2):
    try:
        _imp.release_lock()
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))
    print("held after release", _imp.lock_held())
