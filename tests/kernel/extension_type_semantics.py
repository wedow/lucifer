# What CPython's type machinery and C-API do for extension types, through the
# stdlib's extension modules: a type that disallows instantiation, a C object
# whose state cannot be pickled, a mapping-only type under `in`, and
# PySequence_Fast keeping an iteration's own TypeError.
import bz2
import copy
import pickle
import sqlite3
import xml.etree.ElementTree as ET


def show(f):
    try:
        print('ok', f())
    except Exception as e:
        print(type(e).__name__, e)


it_type = type(ET.Element('a').iter())
show(lambda: it_type())
show(lambda: it_type.__new__(it_type))
show(lambda: copy.copy(ET.Element('a').iter()))
show(lambda: pickle.dumps(bz2.BZ2Compressor()))
show(lambda: copy.copy(bz2.BZ2Decompressor()))


def raising():
    raise TypeError('from the iteration')
    yield


show(lambda: ET.Element('tag').extend(raising()))
show(lambda: ET.Element('tag').extend(5))

db = sqlite3.connect(':memory:')
db.execute('create table t (b blob)')
db.execute('insert into t values (zeroblob(4))')
blob = db.blobopen('t', 'b', 1)
show(lambda: b'a' in blob)
show(lambda: list(blob))
show(lambda: blob[0])
show(lambda: sqlite3.Blob())
blob.close()
