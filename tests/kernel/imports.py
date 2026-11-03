import importlib
import importlib.util
import os
import py_compile
import sys
import tempfile
import zipfile

root = tempfile.mkdtemp()
sys.path.insert(0, root)


def write(name, text, mtime=None):
    path = os.path.join(root, name)
    with open(path, "w") as f:
        f.write(text)
    if mtime is not None:
        os.utime(path, (mtime, mtime))
    return path


def pyc_of(path):
    return importlib.util.cache_from_source(path)


# A pyc is reused while the source mtime and size match, and rebuilt when not.
write("m1.py", "VALUE = 1\n", 1700000000)
import m1
print("first", m1.VALUE, os.path.exists(pyc_of(os.path.join(root, "m1.py"))))
write("m1.py", "VALUE = 2\n", 1700000100)
importlib.invalidate_caches()
del sys.modules["m1"]
import m1
print("mtime changed", m1.VALUE)
write("m1.py", "VALUE = 3\n", 1700000100)  # same mtime, same size: stale pyc wins
del sys.modules["m1"]
import m1
print("same stamp", m1.VALUE)
write("m1.py", "VALUE = 44\n", 1700000100)  # same mtime, new size
del sys.modules["m1"]
import m1
print("size changed", m1.VALUE)

# CHECKED_HASH pycs follow the source contents, not the mtime.
src = write("m2.py", "VALUE = 'a'\n", 1700000000)
py_compile.compile(src, doraise=True,
                   invalidation_mode=py_compile.PycInvalidationMode.CHECKED_HASH)
with open(pyc_of(src), "rb") as f:
    print("flags", int.from_bytes(f.read(8)[4:], "little"))
import m2
print("hash", m2.VALUE)
write("m2.py", "VALUE = 'b'\n", 1700000000)
del sys.modules["m2"]
import m2
print("hash changed", m2.VALUE)

# UNCHECKED_HASH pycs are trusted even when the source changes.
src = write("m3.py", "VALUE = 'x'\n")
py_compile.compile(src, doraise=True,
                   invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
write("m3.py", "VALUE = 'y'\n")
import m3
print("unchecked", m3.VALUE)

# invalidate_caches finds a module created after the finder cached the directory.
try:
    import late
except ImportError as e:
    print("missing", e.name)
write("late.py", "WHERE = 'here'\n")
importlib.invalidate_caches()
import late
print("late", late.WHERE)

# A zip file on sys.path.
archive = os.path.join(root, "bundle.zip")
with zipfile.ZipFile(archive, "w") as z:
    z.writestr("zipped.py", "NAME = __name__\nFILE = __file__.rpartition('/')[2]\n")
    z.writestr("zpkg/__init__.py", "ITEM = 'pkg item'\n")
    z.writestr("zpkg/sub.py", "from . import ITEM\nGOT = ITEM + '!'\n")
sys.path.insert(0, archive)
import zipped
import zpkg.sub
print("zip", zipped.NAME, zipped.FILE, zpkg.sub.GOT)
print(type(zipped.__loader__).__name__)

# reload re-executes the source and keeps the module object.
write("m4.py", "COUNT = globals().get('COUNT', 0) + 1\n", 1700000000)
import m4
before = m4
write("m4.py", "COUNT = globals().get('COUNT', 0) + 10\n", 1700000200)
importlib.invalidate_caches()
again = importlib.reload(m4)
print("reload", again is before, m4.COUNT)

# A package, a relative import, and the spec.
os.mkdir(os.path.join(root, "pk"))
write("pk/__init__.py", "from .inner import f\n")
write("pk/inner.py", "def f(): return __package__\n")
import pk
print("package", pk.f(), pk.__spec__.submodule_search_locations == [os.path.join(root, "pk")])
print(importlib.util.find_spec("nonexistent_module_x"))
