"""An instance's attributes read and written by one thread while another
replaces the instance's __dict__ and keeps making dicts of the same shape. The
replaced dict must stay a dict nobody else owns until every reader that loaded
it lock-free is done (CPython: _PyObject_XDecRefDelayed in
_PyObject_SetManagedDict). The reader must only ever see the values the writer
put in o.__dict__, and no other dict may gain the reader's stores."""
import threading
import time


class C:
    pass


def race(store, seconds):
    o = C()
    o.__dict__ = {'x': 1}
    stop = []
    bad = []

    def reader():
        while not stop:
            try:
                v = o.x
            except AttributeError:
                bad.append('AttributeError')
                return
            if v != 1 and v != 2:
                bad.append('read %r' % (v,))
                return
            if store:
                o.y = 0

    t = threading.Thread(target=reader)
    t.start()
    end = time.monotonic() + seconds
    i = 0
    while time.monotonic() < end and not bad:
        for _ in range(1000):
            o.__dict__ = {'x': 1 + (i & 1)}
            junk = {'x': 'WRONG'}
            if 'y' in junk:
                bad.append('a stranger dict gained y')
                break
            i += 1
    stop.append(True)
    t.join()
    return bad[0] if bad else 'ok'


print('reads', race(False, 1.5))
print('reads and stores', race(True, 1.5))
