---
id: luc-wp37
status: open
deps: []
links: []
created: 2026-11-04T14:42:17Z
type: bug
priority: 2
parent: luc-h4gc
---
# meson cannot find Python.h or libpython through Lucifer's sysconfig

numpy f2py and cython tests build extensions with meson using bin/lucifer as the interpreter: python_info reports include dir build/include/python3.14t, which holds no Python.h, then links -lpython3.14t (static runtime, no libpython), so 'Run-time dependency python found: NO' and about 53 numpytest rows ERROR (f2py test_inplace, test_cython). Needs sysconfig paths that exist (headers from include/, Py_ENABLE_SHARED/LDLIBRARY/LIBDIR consistent with a static binary) and a check that meson setup, ninja and cc build a module that Lucifer imports.

