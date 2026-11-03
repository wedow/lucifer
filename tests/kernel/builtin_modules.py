import _signal
import _string
import atexit
import gc
import marshal

print(list(_string.formatter_parser("a{0}b{x!r:>5}{{c}}{}")))
print(_string.formatter_field_name_split("a.b[0][k]")[0], list(_string.formatter_field_name_split("a.b[0][k]")[1]))
print(_string.formatter_field_name_split("0")[0], list(_string.formatter_field_name_split("name")[1]))
try:
    list(_string.formatter_parser("{"))
except ValueError as e:
    print(e)
try:
    list(_string.formatter_parser("}"))
except ValueError as e:
    print(e)

print(_signal.SIGINT, _signal.SIGTERM, _signal.SIG_DFL, _signal.SIG_IGN, _signal.NSIG)
print(_signal.getsignal(_signal.SIGTERM), _signal.getsignal(_signal.SIGINT) is _signal.default_int_handler)
handler = lambda signum, frame: None
print(_signal.signal(_signal.SIGUSR1, handler), _signal.getsignal(_signal.SIGUSR1) is handler)
print(_signal.signal(_signal.SIGUSR1, _signal.SIG_IGN) is handler)
for call in (lambda: _signal.signal(0, handler), lambda: _signal.getsignal("x"), lambda: _signal.signal(2, 5)):
    try:
        call()
    except (TypeError, ValueError) as e:
        print(type(e).__name__, e)

print(gc.isenabled(), gc.get_threshold(), gc.get_debug(), gc.garbage, gc.callbacks)
gc.disable()
print(gc.isenabled(), gc.collect() >= 0, gc.get_count() >= (0, 0, 0))
gc.enable()
gc.set_threshold(100, 5)
print(gc.get_threshold())
try:
    gc.collect(5)
except ValueError as e:
    print(e)
print(gc.DEBUG_STATS, gc.DEBUG_LEAK)

print(marshal.version, marshal.loads(b"i\x05\x00\x00\x00"), marshal.loads(b"(\x02\x00\x00\x00i\x01\x00\x00\x00z\x02hi"))
print(marshal.loads(memoryview(b"N")), marshal.loads(bytearray(b"T")), marshal.loads(b"F"))
print(atexit.register(print, "x") is print, atexit._ncallbacks())
atexit.unregister(print)
print(atexit._ncallbacks())
atexit.register(lambda: print("ran last"))
atexit._clear()
print(atexit._ncallbacks())


class Fails:
    def __call__(self):
        raise ValueError("from an atexit callback")

    def __repr__(self):
        return "Fails()"


atexit.register(print, "after the failure")
atexit.register(Fails())
