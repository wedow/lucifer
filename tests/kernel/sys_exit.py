import atexit
import sys

for args in ((), (None,), (3,), ("text",), ((1, 2),)):
    try:
        sys.exit(*args)
    except SystemExit as e:
        print(args, e.code, e.args)
try:
    sys.exit(1, 2)
except TypeError as e:
    print(e)
print(sys.exit.__name__, type(sys.exit).__name__)
atexit.register(print, "first registered, runs last")
atexit.register(print, "second registered", "runs first")
atexit.register(lambda: print("third, then", atexit._ncallbacks()))
dropped = lambda: print("never")
atexit.register(dropped)
atexit.unregister(dropped)
print("exiting")
sys.exit(4)
