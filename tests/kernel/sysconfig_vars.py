import sysconfig

for name in ('Py_GIL_DISABLED', 'SOABI', 'EXT_SUFFIX', 'VERSION', 'ABIFLAGS', 'MULTIARCH', 'Py_DEBUG',
             'WITH_PYMALLOC', 'Py_ENABLE_SHARED', 'SHLIB_SUFFIX', 'MACHDEP', 'SIZEOF_LONG', 'SIZEOF_VOID_P',
             'SIZEOF_WCHAR_T', 'HAVE_FORK', 'TZPATH', 'WITH_DOC_STRINGS', 'Py_TRACE_REFS', 'PLATLIBDIR', 'no_such_var'):
    print(name, sysconfig.get_config_var(name))
print(sysconfig.get_platform(), sysconfig.get_python_version(), sysconfig.get_config_var('py_version_short'))
print(sysconfig.get_config_var('EXT_SUFFIX') == sysconfig.get_config_vars()['EXT_SUFFIX'])
