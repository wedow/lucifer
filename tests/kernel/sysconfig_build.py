import os
import sysconfig

# What an extension build (meson, setuptools) asks of the interpreter.
paths = sysconfig.get_paths()
print(os.path.exists(os.path.join(paths['include'], 'Python.h')))
print(os.path.exists(os.path.join(sysconfig.get_config_var('INCLUDEPY'), 'Python.h')))
print(repr(sysconfig.get_config_var('LIBPYTHON')), sysconfig.get_config_var('Py_ENABLE_SHARED'),
      sysconfig.get_config_var('EXT_SUFFIX'), sysconfig.get_config_var('Py_GIL_DISABLED'))
