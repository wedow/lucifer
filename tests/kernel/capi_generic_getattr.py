# PyObject_GenericGetAttr and PyObject_GenericSetAttr run object's
# algorithm whatever the object: on a class they read and write the class's
# own dict through type's __dict__ descriptor, not type's lookup through the
# bases; on a module, type-level data descriptors before its dict.
import ctypes
import sys

get = ctypes.pythonapi.PyObject_GenericGetAttr
get.restype = ctypes.py_object
get.argtypes = [ctypes.py_object, ctypes.py_object]
set_ = ctypes.pythonapi.PyObject_GenericSetAttr
set_.restype = ctypes.c_int
set_.argtypes = [ctypes.py_object, ctypes.py_object, ctypes.py_object]


def show(f, *args):
    try:
        print(repr(f(*args)))
    except BaseException as e:
        print(type(e).__name__, e)


class A:
    x = 1

    def m(self):
        return 'm'


class B(A):
    y = 2


for name in ('x', 'y', 'm', '__name__', '__doc__'):
    print('class', name, end=' ')
    show(get, B, name)

m = type(sys)('m')
m.__dict__['__class__'] = 'shadow'
m.__dict__['v'] = 'v'
for name in ('__class__', 'v', '__dict__', 'missing'):
    print('module', name, end=' ')
    show(get, m, name)
print('instance', end=' ')
show(get, B(), 'x')
show(set_, B(), 'z', 3)
