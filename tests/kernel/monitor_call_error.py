# A monitored CALL_FUNCTION_EX that fails before the call is made leaves
# nothing behind: the next call, in code no tool watches, fires no event.
import sys

mon = sys.monitoring
E = mon.events
log = []
mon.use_tool_id(2, "p")
mon.register_callback(2, E.CALL, lambda code, off, c, a: log.append(getattr(c, "__name__", c)))


def f(*a):
    pass


def run():
    try:
        f(*5)
    except TypeError:
        pass


def main():
    run()
    len("x")


mon.set_local_events(2, run.__code__, E.CALL)
main()
mon.set_local_events(2, run.__code__, 0)
mon.free_tool_id(2)
print(log)
