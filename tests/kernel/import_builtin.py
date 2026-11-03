import errno
import gc
import sys
import time
import _imp
import _thread
import _weakref

print(sys.modules["sys"] is sys, "time" in sys.modules, "os" in sys.modules)
print(time.__name__, time.__spec__.name, time.__spec__.loader, time.__spec__.origin)
print(sys.__spec__.loader.__name__, sys.meta_path[:2])
print(_imp.is_builtin("time"), _imp.is_builtin("os"), _imp.is_frozen("_frozen_importlib"))
print("time" in sys.builtin_module_names, "gc" in sys.builtin_module_names)
print(errno.ENOENT, errno.errorcode[errno.EEXIST])
print(gc.isenabled(), gc.collect() >= 0, gc.get_threshold())
print(time.monotonic() > 0, time.time() > 1e9, time.perf_counter() >= 0)
print(type(_thread.get_ident()).__name__, _thread.get_ident() == _thread.get_ident())
lock = _thread.allocate_lock()
print(lock.acquire(), lock.locked(), lock.acquire(False), lock.release(), lock.locked())


class Box:
    pass


box = Box()
ref = _weakref.ref(box)
print(ref() is box, _weakref.getweakrefcount(box))

import time as t2
from time import monotonic
import sys as s2, errno as e2
print(t2 is time, s2 is sys, e2 is errno, monotonic.__name__)
print(__import__("errno") is errno, __import__("time", fromlist=["sleep"]) is time)

try:
    import no_such_module
except ModuleNotFoundError as e:
    print(e, e.name, e.msg)
    tb = e.__traceback__
    while tb:
        print(tb.tb_frame.f_code.co_name, tb.tb_lineno)
        tb = tb.tb_next
try:
    import no_such.sub
except ImportError as e:
    print(type(e).__name__, e)
try:
    from time import nothing
except ImportError as e:
    print(type(e).__name__, e, e.name)
try:
    __import__("time", globals(), None, (), 1)
except ImportError as e:
    print(type(e).__name__, e)
try:
    __import__("")
except ValueError as e:
    print(type(e).__name__, e)
try:
    __import__("time", level=-1)
except ValueError as e:
    print(type(e).__name__, e)
try:
    __import__(3)
except TypeError as e:
    print(type(e).__name__, e)
import no_such_at_all
