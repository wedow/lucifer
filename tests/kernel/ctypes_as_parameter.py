# A simple type's from_param converts the value itself first and reads
# _as_parameter_ only when that fails, so a py_object argument is the object
# even when it has one (llvmlite passes its execution engine so).
import ctypes


class Ref:
    def __init__(self, param):
        self._as_parameter_ = param


as_type = ctypes.pythonapi.PyObject_Type
as_type.argtypes = [ctypes.py_object]
as_type.restype = ctypes.py_object
print(as_type(Ref(ctypes.py_object(42))).__name__, as_type(7).__name__)
from_long = ctypes.pythonapi.PyLong_FromLong
from_long.argtypes = [ctypes.c_long]
from_long.restype = ctypes.py_object
print(from_long(Ref(ctypes.c_long(5))), from_long(Ref(6)), from_long(Ref(Ref(7))))
for bad in (Ref('x'), 'y', 1.5):
    try:
        from_long(bad)
    except ctypes.ArgumentError as e:
        print('ArgumentError', e)
for t in (ctypes.c_long, ctypes.c_double, ctypes.c_char, ctypes.c_bool, ctypes.c_ubyte, ctypes.c_float):
    for v in ('y', b'yy', None, (), [], {}, 1.5, 2**70, -1, True, Ref(3)):
        try:
            t.from_param(v)
            print(t.__name__, type(v).__name__, 'ok')
        except (TypeError, ValueError, OverflowError) as e:
            print(t.__name__, type(v).__name__, type(e).__name__, e)
