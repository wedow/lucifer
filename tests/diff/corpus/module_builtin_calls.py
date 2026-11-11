"""Functions of the runtime's Python-written modules (struct, json's
encoder, _bisect-like helpers, itertools) called from hot loops as the
builtins they look like: positional and defaulted arguments, results, the
errors they raise (their messages and types), and a traceback's last line."""
import json
import struct
import traceback
from support import loops


def run():
    out = []
    enc = json.JSONEncoder(sort_keys=True)
    for i in range(300):
        out.append(struct.pack('<HbI', i, -i % 100, i * 7))
        out.append(enc.encode({'k': i, 'v': [i, 'x\n' * (i % 3)]}))
        out.append(struct.calcsize('<H'))
    errs = []
    for bad in (('<H', -1), ('<b', 300), ('<Q', 'x')):
        try:
            struct.pack(*bad)
        except struct.error as e:
            errs.append(('error', str(e)))
        except Exception as e:
            errs.append((type(e).__name__, str(e)))
    try:
        enc.encode({1j: 2})
    except TypeError as e:
        errs.append(traceback.format_exception_only(e)[-1])
    return len(out), out[:6], errs


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
