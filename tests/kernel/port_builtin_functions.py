# The functions of a C module are builtin functions: stored in a class they do
# not become methods, and they show as builtins to repr, types and inspect.
import importlib
import inspect
import math
import operator
import pickle
import time
import types
import zlib, gc, atexit, _operator

MODULES = """array atexit binascii _blake2 _codecs _collections _contextvars _ctypes _datetime
errno faulthandler fcntl gc grp _imp _io itertools _locale marshal math _md5 mmap _operator
_pickle posix _posixsubprocess pwd pyexpat _queue _random resource select _sha1 _sha2 _sha3
_signal _socket _sre _string _struct _thread time _tracemalloc _types _typing unicodedata
_warnings _weakref zlib""".split()
for name in MODULES:
    module = importlib.import_module(name)
    functions = sorted(k for k, v in vars(module).items() if type(v) is types.FunctionType)
    if functions:
        print(name, 'has Python functions:', functions)


class A:
    matmul = operator.matmul
    sqrt = math.sqrt
    add = operator.add
    monotonic = time.monotonic


a = A()
print(a.sqrt(4.0), A.sqrt(9.0), a.add(1, 2), a.matmul is operator.matmul, a.monotonic() > 0)

for f in (math.sqrt, math.isclose, operator.add, operator.matmul, _operator._compare_digest, time.monotonic):
    print(repr(f), type(f) is types.BuiltinFunctionType, inspect.isbuiltin(f), inspect.isfunction(f),
          inspect.isroutine(f), f.__self__.__name__, pickle.loads(pickle.dumps(f)) is f)

for f in (math.isclose, math.comb, _operator._compare_digest, zlib.crc32, gc.collect, atexit.register):
    print(f.__name__, f.__text_signature__, inspect.signature(f))
