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

# The modules over optional system libraries, and the C-API they exercise.
import compression.zstd
import curses
import curses.panel
import dbm.gnu
import dbm.ndbm
import decimal
import gc
import lzma
import os
import readline
import syslog
import tempfile
import termios
import tkinter
import uuid

data = b'lucifer' * 100
print('lzma', lzma.decompress(lzma.compress(data)) == data,
      lzma.decompress(lzma.compress(data, format=lzma.FORMAT_ALONE)) == data)
print('zstd', compression.zstd.decompress(compression.zstd.compress(data, level=5)) == data)

D = decimal.Decimal
with decimal.localcontext(prec=30):
    print('decimal', type(D(1)).__module__, D(1) / D(7), D(2).sqrt(), D(10) ** 20)
print('decimal from float', D(3.25), D(-0.1), D('-1.5').as_integer_ratio(), D('12.5E-3').as_integer_ratio())

with tempfile.TemporaryDirectory() as tmp:
    with dbm.gnu.open(os.path.join(tmp, 'g'), 'c') as db:
        db[b'k'] = b'v'
        db['s'] = 'x'
        print('gdbm', sorted(db.keys()), db.get(b'k'), db.get(b'z', b'default'))
    try:
        dbm.gnu.open(os.path.join(tmp, 'absent'), 'r')
    except dbm.gnu.error as e:
        print('gdbm error', type(e.args[0]).__name__)
    with dbm.ndbm.open(os.path.join(tmp, 'n'), 'c') as db:
        db[b'k'] = b'v'
        print('ndbm', db.keys(), db.get(b'k'), db.get(b'z'), db.setdefault(b'k2', b'w'))
        try:
            db.get(key=b'z')
        except TypeError as e:
            print('ndbm keywords', e)
    history = os.path.join(tmp, 'history')
    readline.add_history('one')
    readline.add_history('two')
    readline.write_history_file(history)
    readline.add_history('three')
    readline.append_history_file(1, history)
    readline.clear_history()
    readline.read_history_file(history)
    print('readline', [readline.get_history_item(i + 1) for i in range(readline.get_current_history_length())])

r, w = os.pipe()
try:
    termios.tcgetattr(r)
except termios.error as e:
    print('termios', e.args)
os.close(r)
os.close(w)
print('termios constants', termios.ECHO, termios.TCSANOW, termios.B38400)
master, slave = os.openpty()
attrs = termios.tcgetattr(slave)
attrs[3] &= ~termios.ECHO
termios.tcsetattr(slave, termios.TCSANOW, attrs)
termios.tcsetwinsize(slave, (24, 80))
print('termios pty', os.isatty(slave), termios.tcgetattr(slave)[3] & termios.ECHO, termios.tcgetwinsize(slave),
      os.get_inheritable(master), os.get_inheritable(slave))
os.write(master, b'typed\n')
print('pty read', os.read(slave, 100))
os.close(master)
os.close(slave)

syslog.openlog()
syslog.closelog()
print('syslog', syslog.LOG_MASK(syslog.LOG_ERR), syslog.LOG_UPTO(syslog.LOG_WARNING))

print('uuid', uuid._generate_time_safe is not None, uuid.uuid1().version)

print('curses', curses.ncurses_version.major >= 6, type(curses.ncurses_version).__name__)
try:
    type(curses.ncurses_version)((6, 0, 0))
except TypeError as e:
    print('curses version type', e)
print('curses.panel', curses.panel.version)
gc.collect()

tcl = tkinter.Tcl()
print('tcl', tcl.eval('expr {6*7}'), tcl.call('string', 'toupper', 'abc'), tcl.splitlist('a {b c} d'))
print('tcl ints', tcl.call('expr', -2 ** 70), tcl.getint(tcl.call('expr', '2**65')))
print('tcl surrogate', tcl.call('string', 'length', 'a\udcffb'))
