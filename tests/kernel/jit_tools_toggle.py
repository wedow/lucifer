# A tool switched on and off once, before the hot run: sys.settrace,
# sys.setprofile, or sys.monitoring events of a tool id, then None; or
# only sys.settrace(sys.gettrace()), which restores no tool at all. The
# functions and kernel builtins already hot before the toggle, and the
# ones first called after it, run as they did before it. tools/jitcheck's
# toolsoff lane runs this at two sizes for each tool under
# -X lucifer_jit=always and requires what holds with no toggle: the hot
# loops run in compiled code with their callees in line, so entries into
# compiled code and direct calls do not grow with the iterations (an
# interpreted loop enters the kernel builtins' units three times an
# iteration), and the kernel units do not leave at their polls.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
TOOL = sys.argv[2] if len(sys.argv) > 2 else 'settrace'


def callee(a, b):
    return a + b


class K:
    def m(self, x):
        return x * 2


def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)


def hot(fn, bound, n):
    s = 0
    for i in range(n):
        s += fn(i, 1)
        s += bound(i)
        s += max(i, 7)
        s += sum(ITEMS)
    return s


ITEMS = list(range(20))
print(hot(callee, K().m, 200), fib(12))


def tracer(frame, event, arg):
    return tracer


def toggle(tool):
    if tool == 'settrace':
        sys.settrace(tracer)
        callee(1, 2)
        sys.settrace(None)
    elif tool == 'setprofile':
        sys.setprofile(tracer)
        callee(1, 2)
        sys.setprofile(None)
    elif tool == 'monitoring':
        mon = sys.monitoring
        mon.use_tool_id(3, 'toggle')
        mon.register_callback(3, mon.events.PY_START, lambda code, offset: None)
        mon.set_events(3, mon.events.PY_START)
        callee(1, 2)
        mon.set_events(3, 0)
        mon.register_callback(3, mon.events.PY_START, None)
        mon.free_tool_id(3)
    elif tool == 'restore':
        sys.settrace(sys.gettrace())


toggle(TOOL)


def later(a, b):
    return a - b


def hot_later(fn, n):
    s = 0
    for i in range(n):
        s += fn(i, 1)
        s += min(i, 5)
    return s


print(hot(callee, K().m, N), hot_later(later, N), fib(15))
