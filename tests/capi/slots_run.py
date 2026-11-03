import importlib, sys

NAMES = '''slots_plain slots_noshare slots_shared_gil slots_create slots_two_exec slots_unknown
slots_negative slots_two_create slots_two_interp slots_two_gil slots_not_module
slots_not_module_exec slots_create_fails slots_create_raises slots_exec_fails slots_exec_raises'''.split()

for name in NAMES:
    try:
        m = importlib.import_module(name)
    except BaseException as e:
        print(name, 'FAILED', type(e).__name__, e, '|', type(e.__cause__).__name__, e.__cause__,
              name in sys.modules)
        continue
    print(name, type(m).__name__, m.__name__, m.__doc__, getattr(m, 'executed', None),
          getattr(m, 'created_by', None), m.state(), m.state(), m.state.__module__,
          m.__spec__.name, type(m.__spec__.loader).__name__, m.__spec__.origin.rsplit('/', 1)[1] == name + '.cpython-314t-x86_64-linux-gnu.so',
          m.__loader__ is m.__spec__.loader, sorted(k for k in vars(m) if not k.startswith('__')))

# importing again returns the cached module; reload executes the module again
import slots_plain, slots_create
print(importlib.import_module('slots_plain') is slots_plain, importlib.reload(slots_plain) is slots_plain, slots_plain.state())
print(sys.modules['slots_create'] is slots_create)
try:
    importlib.import_module('slots_missing')
except ImportError as e:
    print(type(e).__name__, e)
