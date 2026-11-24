# Imports inside hot functions: the function stays compiled and the
# import runs as a call from compiled code (the import system is Python
# and may run anything). Covered, inside the hot loop: a module already
# imported, a first import, `from m import a, b`, a level-0 submodule
# import, a module that replaces itself in sys.modules, a lazy attribute
# (a module __getattr__) taken with `from`, a relative import in a package
# function (level > 0, resolved through that function's own globals) and
# an import in a callee of another module, both inlined into a caller with
# other globals, builtins.__import__ replaced by a wrapper for a stretch of
# the loop and restored, a module whose body rebinds a global the
# importing function reads right after the import, one whose body starts
# and joins a thread, a circular import's error, and `from p import sub`
# taken from sys.modules when the package lacks the attribute. The module
# bodies run a fixed number of times whatever the size. tools/jitcheck
# runs this at two sizes and requires that side exits and entries left to
# the interpreter do not grow with the iterations, and that no function
# here is retired. The ImportError a function catches itself in a function
# of its own runs a few times after the hot loop: an exception taken by a
# handler of the same user code still leaves compiled code (luc-kyqx),
# which is not this program's subject.
import builtins
import os
import sys
import tempfile

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
FLAG = 0

where = tempfile.mkdtemp()
sys.path.insert(0, where)


def write(path, text):
    full = os.path.join(where, *path.split('/'))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w') as f:
        f.write(text)


write('jit_import_selfrep.py', 'import sys\n'
      'class Replacement:\n'
      '    value = 41\n'
      '    def bump(self, x):\n'
      '        return x + self.value\n'
      'sys.modules[__name__] = Replacement()\n')
write('jit_import_lazy.py', 'loads = 0\n'
      'def __getattr__(name):\n'
      '    global loads\n'
      '    if name == "lazy":\n'
      '        loads += 1\n'
      '        return 7\n'
      '    raise AttributeError(name)\n')
write('jit_import_first.py', 'first = 3\n')
write('jit_import_pkg/__init__.py', '')
write('jit_import_pkg/sub.py', 'val = 5\n')
write('jit_import_pkg/inner.py', 'def rel(i):\n'
      '    from . import sub\n'
      '    from .sub import val\n'
      '    return sub.val + val + i\n')
write('jit_import_other.py', 'def other(i):\n'
      '    import os.path\n'
      '    from os import sep\n'
      '    return len(os.path.sep) + len(sep) + i\n')
write('jit_import_rebind.py', 'import __main__\n'
      '__main__.FLAG += 1\n')
write('jit_import_thread.py', 'import threading\n'
      'ran = []\n'
      't = threading.Thread(target=ran.append, args=(1,))\n'
      't.start()\n'
      't.join()\n')
write('jit_import_circ_a.py', 'from jit_import_circ_b import b\n'
      'thing = 1\n')
write('jit_import_circ_b.py', 'from jit_import_circ_a import thing\n'
      'b = 2\n')
write('jit_import_fpkg/__init__.py', '')
write('jit_import_fpkg/sub.py', 'n = 9\n')

import jit_import_fpkg.sub
del jit_import_fpkg.sub     # the package lacks the attribute; sys.modules has it
from jit_import_pkg.inner import rel
from jit_import_other import other


def already(i):
    import os
    return len(os.sep) + i


def names(i):
    from os.path import join, sep
    return len(join('a', 'b')) + len(sep) + i


def submodule(i):
    import os.path
    return len(os.path.sep) + i


def missing(i):
    try:
        import jit_import_not_there
    except ImportError as e:
        return len(e.name)
    return -1


def selfrep(i):
    import jit_import_selfrep
    return jit_import_selfrep.bump(i)


def lazy(i):
    from jit_import_lazy import lazy
    return lazy + i


def first(i):
    if i < 100:
        return i
    import jit_import_first
    return jit_import_first.first + i


def fallback(i):
    from jit_import_fpkg import sub
    return sub.n + i


def rebind(i):
    sys.modules.pop('jit_import_rebind', None)
    import jit_import_rebind
    return FLAG


def threaded(i):
    sys.modules.pop('jit_import_thread', None)
    import jit_import_thread
    return len(jit_import_thread.ran)


def circular(i):
    sys.modules.pop('jit_import_circ_a', None)
    sys.modules.pop('jit_import_circ_b', None)
    try:
        import jit_import_circ_a
    except ImportError as e:
        return e.args[0].split(' (')[0] + (' circular' if 'circular import' in e.args[0] else '')
    return 'imported'


wrapped = []
real_import = builtins.__import__


def wrapper(name, *args, **kwargs):
    wrapped.append(name)
    return real_import(name, *args, **kwargs)


def run(n):
    s = 0
    every = max(1, n // 8)
    messages = set()
    for i in range(n):
        s += already(i) + names(i) + submodule(i) + selfrep(i) + lazy(i) + first(i)
        s += rel(i) + other(i) + fallback(i)
        if i == n // 2 + 3:
            builtins.__import__ = wrapper       # a stretch with no module body run in it
        elif i == n // 2 + 103:
            builtins.__import__ = real_import
        if i % every == 1:
            s += rebind(i) + threaded(i)
            messages.add(circular(i))
    return s, sorted(messages)


print(run(N))
print(FLAG, len(wrapped), sorted(set(wrapped)))
print(sum(missing(i) for i in range(3)))
import jit_import_lazy
print(jit_import_lazy.loads == N)
print(type(sys.modules['jit_import_selfrep']).__name__)
import shutil
shutil.rmtree(where)
