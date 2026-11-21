"""The code image's strs and constants behave as freshly made ones (luc-uqiy):
interning a str equal to a frozen module's name yields the one object, str
hashes follow PYTHONHASHSEED in a child exactly as CPython's do, frozenset
constants work, and a frozen module imports again."""
import subprocess
import sys

name = "".join(["__in", "it__"])
literal = "__init__"
print(sys.intern(name) is literal, sys.intern(name) is sys.intern("__init__"))
print(sys.intern("".join(["path", "sep"])) is sys.intern("pathsep"))
print(hash(name) == hash("__init__"), hash("__init__") == hash(object.__init__.__name__))

src = ("import os; print(hash('__init__'), hash('os'), hash('_frozen_importlib'), hash(b'abc'), "
       "hash(('__name__', 1)), hash(frozenset({'a', 'b'})) == hash(frozenset(['b', 'a'])))")
for seed in ("0", "1", "12345"):
    out = subprocess.run([sys.executable, "-S", "-c", src], env={"PYTHONHASHSEED": seed},
                         capture_output=True, text=True)
    print(seed, out.stdout.strip(), out.returncode)

x = 3
print(x in {1, 2, 3}, "b" in {"a", "b"}, sorted(frozenset({"z", "y"})))

import importlib
import keyword
del sys.modules["keyword"]
again = importlib.import_module("keyword")
print(again is not keyword, again.iskeyword("for"))
