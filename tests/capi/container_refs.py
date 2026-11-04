# The *Ref read variants against a concurrent mutator: PyList_GetItemRef and
# PyDict_GetItemRef hand the reader a whole item as a strong reference or a
# clean miss (IndexError; the KeyError class for dict_getitemref, which
# returns it instead of raising), whatever the interleaving. The reader makes
# a fixed number of reads and the mutator a fixed number of bounded cycles, so
# only aggregates that hold for every schedule are printed: one always-miss
# index and one never-inserted key measure the miss path a fixed 500 times,
# and every other read must land in its legal category.
import threading
import _testcapi
import _testlimitedcapi

get_item_ref = _testlimitedcapi.list_get_item_ref
getitem_ref = _testcapi.dict_getitemref

# A list only the reader touches: items in range, then the out-of-range
# errors (negative indices included: PyList_GetItemRef takes none).
L = [10, 20, 30]
print('solo', [get_item_ref(L, i) for i in (0, 2)])
for i in (3, -1, -4):
    try:
        get_item_ref(L, i)
    except IndexError as e:
        print('solo range', i, e)

# PyDict_GetItemRef's miss contract: the wrapper returns the KeyError class.
print('missing', getitem_ref({}, 'missing') is KeyError)

# The error contracts; the non-dict SystemError's message carries the calling
# source's path, which differs per build, so only its type is printed.
try:
    get_item_ref('x', 0)
except TypeError as e:
    print('list type', e)
try:
    getitem_ref([], 'x')
except SystemError:
    print('dict type SystemError')
try:
    getitem_ref({}, [])
except TypeError as e:
    print('dict hash', e)

# The list reader owns indices 0..63 (index i holds i); the mutator owns the
# values beyond them (appends 1000..1007, so index i there holds 1000+i-64)
# and clears the whole list under the reader, then restores it.
R = list(range(64))


def churn_list():
    for _ in range(300):
        R.extend(range(1000, 1008))
        for _ in range(8):
            R.pop()
        R.clear()
        R.extend(range(64))


def read_list():
    # index 99 is never in range, so it must raise IndexError every read
    bad = missed = 0
    for _ in range(500):
        for i in (0, 1, 31, 63, 64, 70, 99):
            try:
                v = get_item_ref(R, i)
            except IndexError:
                missed += i == 99
                continue
            bad += i == 99 or v != (i if i < 64 else 1000 + i - 64)
    return bad, missed


w = threading.Thread(target=churn_list)
w.start()
bad, missed = read_list()
w.join()
print('list concurrent bad', bad, 'always-miss reads', missed)
print('list final', R == list(range(64)))

# The dict reader owns the keys a..f, each mapped to one object the mutator
# puts back; 'never' is never inserted. The mutator churns other keys and
# removes one read key per cycle.
D = {k: (k,) for k in 'abcdef'}
VALUES = dict(D)


def churn_dict():
    for i in range(3000):
        k = 'x%d' % (i % 8)
        D[k] = i
        del D[k]
        k = 'abcdef'[i % 6]
        D[k] = D.pop(k)


def read_dict():
    # 'never' is never inserted, so it must come back as KeyError every read;
    # a..f come back as their one object or as the KeyError class while the
    # mutator holds one popped
    bad = missed = 0
    for _ in range(500):
        for k in ('a', 'b', 'c', 'd', 'e', 'f', 'never'):
            v = getitem_ref(D, k)
            if v is KeyError:
                missed += k == 'never'
            else:
                bad += k == 'never' or v is not VALUES[k]
    return bad, missed


w = threading.Thread(target=churn_dict)
w.start()
bad, missed = read_dict()
w.join()
print('dict concurrent bad', bad, 'always-miss reads', missed)
print('dict final', sorted(D) == list('abcdef'), all(D[k] is VALUES[k] for k in D))
