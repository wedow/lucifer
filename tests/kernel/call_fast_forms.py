# What CALL answers itself must behave as the general call does: type(x)
# ignores __class__, bool(x) runs __bool__ and __len__ with their errors, a
# builtin's own recursion runs out of stack, a signal raised by a C call is
# handled right after it, and sys.monitoring switched on mid-loop sees the
# calls and the C returns.
import signal, sys

class Masked:
    @property
    def __class__(self):
        return int
m = Masked()
print(type(m) is Masked, m.__class__, isinstance(m, int), [type(x).__name__ for x in (m, 1, "a", None)])

class Truth:
    def __init__(self, v):
        self.v = v
    def __bool__(self):
        if self.v == "raise":
            raise ZeroDivisionError("no truth")
        return self.v
class Sized:
    def __init__(self, n):
        self.n = n
    def __len__(self):
        return self.n
for x in (Truth(True), Truth(False), Truth("raise"), Truth(1), Sized(0), Sized(3), Sized(-1), 0, "", [0], None):
    try:
        print(bool(x))
    except Exception as e:
        print(type(e).__name__, e)

# A builtin recursing on its own runs out of stack, not of the recursion limit.
nested = []
for i in range(200000):
    nested = [nested]
try:
    repr(nested)
except RecursionError as e:
    print("RecursionError", str(e).split()[0])
print(len(repr([[[1]]])))

# A signal raised by a C call is handled before the loop goes much further.
seen = []
signal.signal(signal.SIGUSR1, lambda signum, frame: seen.append(i))
for i in range(50):
    len("abc")
    "x".upper()
    if i == 10:
        signal.raise_signal(signal.SIGUSR1)
        print("raised", seen)
print(seen)

# sys.monitoring: CALL and C_RETURN events from the middle of a loop on.
mon = sys.monitoring
TOOL = 3
mon.use_tool_id(TOOL, "calls")
events = []
def on_call(code, offset, callable, arg0):
    events.append(("call", getattr(callable, "__name__", "?")))
def on_c_return(code, offset, callable, arg0):
    events.append(("c_return", getattr(callable, "__name__", "?")))
def on_c_raise(code, offset, callable, arg0):
    events.append(("c_raise", getattr(callable, "__name__", "?")))
def two(a, b):
    return a + b
for i in range(6):
    if i == 3:
        mon.register_callback(TOOL, mon.events.CALL, on_call)
        mon.register_callback(TOOL, mon.events.C_RETURN, on_c_return)
        mon.register_callback(TOOL, mon.events.C_RAISE, on_c_raise)
        mon.set_events(TOOL, mon.events.CALL | mon.events.C_RETURN | mon.events.C_RAISE)
    two(i, 1)
    len("ab")
    type(i)
    bool(i)
mon.set_events(TOOL, 0)
mon.free_tool_id(TOOL)
print(events)
