import ctypes


set_item = ctypes.pythonapi.PyDict_SetItem
set_item.argtypes = [ctypes.py_object, ctypes.py_object, ctypes.py_object]
set_item.restype = ctypes.c_int
set_item_string = ctypes.pythonapi.PyDict_SetItemString
set_item_string.argtypes = [ctypes.py_object, ctypes.c_char_p, ctypes.py_object]
set_item_string.restype = ctypes.c_int
del_item = ctypes.pythonapi.PyDict_DelItem
del_item.argtypes = [ctypes.py_object, ctypes.py_object]
del_item.restype = ctypes.c_int
del_item_string = ctypes.pythonapi.PyDict_DelItemString
del_item_string.argtypes = [ctypes.py_object, ctypes.c_char_p]
del_item_string.restype = ctypes.c_int


def show_error(operation, *args):
    try:
        operation(*args)
    except (SystemError, TypeError) as exc:
        # CPython can embed the absolute build directory in __FILE__.
        message = str(exc)
        _, source, rest = message.rpartition("/Objects/dictobject.c:")
        if source:
            message = source[1:] + rest
        print(operation.__name__, type(exc).__name__, message)
    else:
        raise AssertionError(f"{operation.__name__} accepted a non-dict")


for setitem, delitem, key in (
    (set_item, del_item, "key"),
    (set_item_string, del_item_string, b"key"),
):
    mapping = {}
    print(setitem(mapping, key, 2), mapping)
    print(delitem(mapping, key), mapping)
    show_error(setitem, [], key, 2)
    show_error(delitem, [], key)

# CPython checks the setter's target before hashing and the deleter's after.
show_error(set_item, [], [], 2)
show_error(del_item, [], [])
