# What __index__ returns is checked wherever a library module converts an
# index (PyNumber_Index): a non-int is a TypeError, an int subclass warns.
import _thread
import collections
import ctypes
import grp
import itertools
import math
import mmap
import os
import pwd
import re
import time
import warnings
from array import array


class Float:
    def __index__(self):
        return 2.7


class Str:
    def __index__(self):
        return '1'


class Sub(int):
    pass


class Subclass:
    def __index__(self):
        return Sub(1)


def attempt(label, f):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter('always')
        try:
            result = f()
        except Exception as e:
            result = '%s: %s' % (type(e).__name__, e)
    print(label, result, [(w.category.__name__, str(w.message)) for w in caught])


def deque_set(d, i):
    d[i] = 5
    return list(d)


def deque_del(d, i):
    del d[i]
    return list(d)


def array_set(a, i, v):
    a[i] = v
    return a.tolist()


def array_del(a, i):
    del a[i]
    return a.tolist()


m = mmap.mmap(-1, 4)
for key in (Float(), Str(), Subclass()):
    name = type(key).__name__
    attempt(name + ' array get', lambda: array('i', [10, 20, 30])[key])
    attempt(name + ' array set', lambda: array_set(array('i', [10, 20, 30]), key, 5))
    attempt(name + ' array del', lambda: array_del(array('i', [10, 20, 30]), key))
    attempt(name + ' array value', lambda: array_set(array('i', [10, 20, 30]), 0, key))
    attempt(name + ' deque get', lambda: collections.deque([10, 20, 30])[key])
    attempt(name + ' deque set', lambda: deque_set(collections.deque([10, 20, 30]), key))
    attempt(name + ' deque del', lambda: deque_del(collections.deque([10, 20, 30]), key))
    attempt(name + ' deque rotate', lambda: collections.deque([10, 20, 30]).rotate(key))
    attempt(name + ' islice', lambda: list(itertools.islice('abc', key)))
    attempt(name + ' repeat', lambda: list(itertools.repeat('x', key)))
    attempt(name + ' math.factorial', lambda: math.factorial(key))
    attempt(name + ' math.comb', lambda: math.comb(3, key))
    attempt(name + ' mmap get', lambda: m[key])
    attempt(name + ' ctypes', lambda: ctypes.c_int(key).value)
    attempt(name + ' time.gmtime', lambda: time.gmtime(key).tm_sec)
    attempt(name + ' grp.getgrgid', lambda: grp.getgrgid(key).gr_gid)
    attempt(name + ' pwd.getpwuid', lambda: pwd.getpwuid(key).pw_uid)
    attempt(name + ' stack_size', lambda: _thread.stack_size(key))
    attempt(name + ' waitstatus', lambda: os.waitstatus_to_exitcode(key))
    attempt(name + ' re pos', lambda: re.compile('b').search('abc', key).span())
m.close()
