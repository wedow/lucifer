# The standard library's C extension modules, built from CPython's own source,
# import from lib-dynload and work: each line is one module's observable result.
import bz2
import cmath
import codecs
import multiprocessing.synchronize
import sqlite3
import ssl
import xml.etree.ElementTree as ET
import xml.parsers.expat

print('bz2', bz2.decompress(bz2.compress(b'lucifer' * 100)) == b'lucifer' * 100)
print('cmath', cmath.sqrt(-4), cmath.phase(-1), cmath.isclose(cmath.exp(1j * cmath.pi), -1))

db = sqlite3.connect(':memory:')
db.execute('create table t (a, b)')
db.executemany('insert into t values (?, ?)', [(i, str(i) * i) for i in range(5)])
print('sqlite3', db.execute('select sum(a), group_concat(b) from t').fetchone())

context = ssl.create_default_context()
print('ssl', context.verify_mode, context.check_hostname, ssl.OPENSSL_VERSION_INFO >= (1, 1))

tree = ET.fromstring('<a x="1"><b>text</b><b/></a>')
print('elementtree', type(tree).__module__, tree.attrib, [b.text for b in tree.iter('b')])
events = []
parser = xml.parsers.expat.ParserCreate()
parser.StartElementHandler = lambda name, attrs: events.append((name, attrs))
parser.Parse('<r k="v"><c/></r>', True)
print('expat', events)

for encoding in ('gbk', 'shift_jis', 'big5', 'euc_kr', 'iso2022_jp', 'big5hkscs'):
    text = '北京市' if encoding != 'euc_kr' else '서울'
    data = text.encode(encoding)
    print(encoding, data, data.decode(encoding) == text)
print('incremental', codecs.getincrementaldecoder('gbk')().decode('北'.encode('gbk')[:1]))

lock = multiprocessing.synchronize.Lock(ctx=multiprocessing.get_context('fork'))
with lock:
    print('multiprocessing', lock._semlock._count())
