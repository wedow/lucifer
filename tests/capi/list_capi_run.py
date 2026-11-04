# The list C-API: PyList_Append holds the list's lock, so appends from
# several threads lose none, and it finds a list being sorted emptied, as
# Python's append does; PyList_GetItemRef against a thread that resizes the
# list and frees its items returns a whole item or raises IndexError.
import threading
import _testlimitedcapi as c


def run(target, n):
    out = []
    ts = [threading.Thread(target=lambda k=k: out.append(target(k))) for k in range(n)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    return out


A = []


def append(k):
    for i in range(20000):
        c.list_append(A, (k, i))


run(append, 4)
print('appended', len(A), len(set(A)))

S = [3, 1, 2]
try:
    S.sort(key=lambda x: c.list_append(S, 0) or x)
except ValueError as e:
    print('sort', e)
print(S)

R = [(i, i) for i in range(64)]
stop = []


def resize():
    while not stop:
        R.extend([(i, i) for i in range(300)])
        for _ in range(250):
            R.pop()
        del R[40:]
        R[:] = [(i, i) for i in range(5)]
        R.clear()
        R.extend([(i, i) for i in range(64)])


def get_item_ref(k):
    bad = 0
    for _ in range(20000):
        for i in (0, 3, 40, 63, 200):
            try:
                v = c.list_get_item_ref(R, i)
            except IndexError:
                continue
            bad += not (type(v) is tuple and len(v) == 2 and v[0] == v[1])
    return bad


w = threading.Thread(target=resize)
w.start()
torn = run(get_item_ref, 3)
stop.append(True)
w.join()
print('get_item_ref', torn)
