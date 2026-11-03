# sys's hooks and small functions.
import sys
import builtins

try:
    1 / 0
except ZeroDivisionError:
    sys.excepthook(*sys.exc_info())
print(sys.excepthook is sys.__excepthook__, sys.displayhook is sys.__displayhook__,
      sys.breakpointhook is sys.__breakpointhook__, sys.unraisablehook is sys.__unraisablehook__)
sys.displayhook(None)
sys.displayhook([1, 'a'])
print(builtins._)
del builtins._
print(sys.getsizeof(1) > 0, sys.getsizeof(object(), 5) > 0, sys.getsizeof(object.__new__ if False else 1, 2) > 0)


class NoSize:
    __sizeof__ = None


print(sys.getsizeof(NoSize(), 'default'))
try:
    sys.getsizeof(NoSize())
except TypeError as e:
    print('TypeError')
try:
    sys.setswitchinterval(0)
except ValueError as e:
    print(e)
sys.setswitchinterval(0.01)
print(sys.gettrace(), sys.getprofile())
f = lambda *a: None
sys.settrace(f)
sys.setprofile(f)
print(sys.gettrace() is f, sys.getprofile() is f)
sys.settrace(None)
sys.setprofile(None)
print(sys.call_tracing(max, (1, 2)), sys.get_coroutine_origin_tracking_depth())
sys.set_coroutine_origin_tracking_depth(2)
print(sys.get_coroutine_origin_tracking_depth())
try:
    sys.set_coroutine_origin_tracking_depth(-1)
except ValueError as e:
    print(e)
print(sys.getdlopenflags(), 'os' in sys.stdlib_module_names, 'sys' in sys.stdlib_module_names,
      type(sys.stdlib_module_names).__name__, len(sys.stdlib_module_names) > 200)
print(sys._getframemodulename(0), sys._get_cpu_count_config())
print(list(sys._current_frames()) == [sys._current_frames and __import__('_thread').get_ident()])
