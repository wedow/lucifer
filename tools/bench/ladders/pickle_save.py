"""pickle_pure_python's save() path, one line of it per level. Usage: pickle_save.py CASE WARM REPS
Each rep runs the level over the str and int keys and values of the benchmark's DICT (OBJS),
on a pure-Python Pickler with a fresh frame and memo, as each dumps() has."""
import sys
sys.modules['_pickle'] = None
import io, pickle
from struct import pack

DICT = {'ads_flags': 0, 'age': 18, 'bulletin_count': 0, 'comment_count': 0, 'country': 'BR',
        'encrypted_id': 'G9urXXAJwjE', 'favorite_count': 9, 'first_name': '', 'flags': 412317970704,
        'friend_count': 0, 'gender': 'm', 'gender_for_display': 'Male', 'id': 302935349,
        'is_custom_profile_icon': 0, 'last_name': '', 'locale_preference': 'pt_BR', 'member': 0,
        'profile_foo_id': 827119638, 'secure_encrypted_id': 'Z_xxx2dYx3t4YAdnmfgyKw', 'session_number': 2,
        'signup_id': '201-19225-223', 'status': 'A', 'theme': 1, 'time_created': 1225237014,
        'time_updated': 1233134493, 'unread_message_count': 0, 'user_group': '0', 'username': 'collinwinter',
        'play_count': 9, 'view_count': 7, 'zip': ''}
OBJS = [x for kv in DICT.items() for x in kv]
STRS = [o for o in OBJS if type(o) is str]
ENC = [s.encode('utf-8', 'surrogatepass') for s in STRS]
NS = [len(e) for e in ENC]
P = pickle._Pickler(io.BytesIO(), 5)
P.framer.start_framing()
_NoValue = getattr(pickle, "_NoValue", None)
SBU = pickle.SHORT_BINUNICODE

def fresh():
    P.framer.start_framing()
    P.memo.clear()

def empty():
    fresh()
    for o in OBJS:
        pass

def empty_s():
    fresh()
    for o in STRS:
        pass

def commit():
    fresh()
    cf = P.framer.commit_frame
    for o in OBJS:
        cf()

def pid():
    fresh()
    for o in OBJS:
        P.persistent_id(o)

def memo_get():
    fresh()
    memo = P.memo
    for o in OBJS:
        memo.get(id(o))

def getattr_default():
    fresh()
    for o in OBJS:
        getattr(P, "reducer_override", _NoValue)

def dispatch():
    fresh()
    for o in OBJS:
        P.dispatch.get(type(o))

def encode():
    fresh()
    for o in STRS:
        o.encode('utf-8', 'surrogatepass')

def pack_b():
    fresh()
    for n in NS:
        pack("<B", n)

def concat():
    fresh()
    for e in ENC:
        SBU + b'\x05' + e

def write():
    fresh()
    w = P.write
    for e in ENC:
        w(e)

def memo_store():
    fresh()
    memo = P.memo
    i = 0
    for o in OBJS:
        memo[id(o)] = i, o
        i += 1

def save():
    fresh()
    s = P.save
    for o in OBJS:
        s(o)

if __name__ == "__main__":
    f = globals()[sys.argv[1]]
    warm, reps = int(sys.argv[2]), int(sys.argv[3])
    for i in range(warm + reps):
        for _ in range(100):
            f()
