"""memoryview alias release."""
from support import loops


def run():
    b = bytearray(b'abcd'); v = memoryview(b); v[1:3] = b'XY'; before = v.tobytes()
    try: b.append(1)
    except BufferError as e: error = type(e).__name__
    v.release(); b.append(33)
    return before, bytes(b), error


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
