import _thread
import contextvars
import ctypes


get_var = ctypes.pythonapi.PyContextVar_Get
get_var.argtypes = [ctypes.py_object, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
get_var.restype = ctypes.c_int
decref = ctypes.pythonapi.Py_DecRef
decref.argtypes = [ctypes.c_void_p]
decref.restype = None
watcher_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_void_p)
add_watcher = ctypes.pythonapi.PyContext_AddWatcher
add_watcher.argtypes = [watcher_type]
add_watcher.restype = ctypes.c_int
clear_watcher = ctypes.pythonapi.PyContext_ClearWatcher
clear_watcher.argtypes = [ctypes.c_int]
clear_watcher.restype = ctypes.c_int


def get(var, *default):
    result = ctypes.c_void_p()
    assert get_var(var, id(default[0]) if default else None, ctypes.byref(result)) == 0
    if result.value is None:
        return '<NULL>'
    try:
        return ctypes.py_object.from_address(ctypes.addressof(result)).value
    finally:
        decref(result)


var = contextvars.ContextVar('var', default='var default')
unset = contextvars.ContextVar('unset')


print(get(var), get(var, 'call default'), get(var, None), get(unset))
print(get(unset, 'first'), get(unset, 'second'), get(unset))
missing = contextvars.Token.MISSING
print(get(contextvars.ContextVar('token default', default=missing)) is missing)

token = var.set('main')
copy = contextvars.copy_context()
print(get(var), get(var, 'ignored'), copy.run(get, var))
new_token = var.set('updated')
print(get(var), copy.run(get, var), get(var))
print(contextvars.Context().run(get, var), get(var))
var.reset(new_token)
print(get(var))


def work():
    # After a miss on a fresh thread, exiting an explicit context restores None.
    ctx = contextvars.Context()
    switches = []

    @watcher_type
    def watcher(event, address):
        switches.append((event, address == id(ctx), address == id(None)))
        return 0

    watcher_id = add_watcher(watcher)
    try:
        print(get(var), get(var, 'thread default'))
        print(ctx.run(get, var), switches)
    finally:
        clear_watcher(watcher_id)
    var.set(None)
    print(get(var), copy.run(get, var), get(var))


_thread.start_joinable_thread(work).join()
print(get(var))
var.reset(token)
print(get(var), copy.run(get, var))

try:
    get({})
except TypeError as exc:
    print(type(exc).__name__, str(exc))
