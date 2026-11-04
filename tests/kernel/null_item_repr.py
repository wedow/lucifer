# The uninitialized list and tuple slots print <NULL> in a repr, and a
# "<NULL>" string a program holds - interned, say - stays quoted (the slot's
# answer is a private object, never a string value).
import sys

from _testlimitedcapi import list_new, tuple_new

s = sys.intern("<NULL>")
print(repr([s]), repr((s,)), repr([s, 1, s]), repr(s))
print(s is sys.intern("<NULL>"))
print(repr(list_new(2)), repr(tuple_new(3)))
