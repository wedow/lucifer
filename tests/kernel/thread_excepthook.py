import io
import re
import sys
import threading

err = io.StringIO()


def run(target, *a):
    t = threading.Thread(target=target, args=a)
    t.start()
    t.join()
    print(re.sub(r'File "[^"]*/threading\.py"', 'File "threading.py"', err.getvalue()), end='')
    err.seek(0)
    err.truncate()


def boom():
    raise ValueError("boom")


def nested():
    try:
        raise KeyError("inner")
    except KeyError:
        raise RuntimeError("outer")


def exits():
    raise SystemExit(3)


class Odd(Exception):
    def __str__(self):
        return "odd"


def odd():
    raise Odd()


sys.stderr = err
for fn in (boom, nested, exits, odd):
    run(fn)
    print("after", fn.__name__)

seen = []
threading.excepthook = lambda a: seen.append(
    (a.exc_type.__name__, str(a.exc_value), a.exc_traceback is not None, a.thread.name))
run(boom)
run(nested)
run(exits)
print(seen)


def bad(args):
    raise TypeError("hook failed")


threading.excepthook = bad
run(boom)
print("done")
