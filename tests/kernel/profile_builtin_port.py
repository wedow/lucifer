# _imp is a builtin module: the profiler sees its functions as C calls that
# return, with no Python frames inside, as profile.py's checks expect.
import _imp
import sys

log = []
sys.setprofile(lambda frame, event, arg: log.append(
    (event, arg.__name__ if event.startswith("c_") else frame.f_code.co_name)))
_imp.acquire_lock()
_imp.release_lock()
_imp.is_builtin("sys")
sys.setprofile(None)
print(log)
