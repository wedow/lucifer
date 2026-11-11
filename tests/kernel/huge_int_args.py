# Arguments CPython takes as Py_ssize_t or C int are range-checked first; a
# huge count never drives a loop.
import math


def t(label, f):
    try:
        print(label, repr(f()))
    except Exception as e:
        print(label, type(e).__name__, e)


for length, order in ((2**63 - 2, 'little'), (2**63, 'big'), (-2**63 - 1, 'mid'), (2**63, 'mid'),
                      (-1, 'mid'), (-1, 'big'), (3, 'big')):
    t(('to_bytes', length, order), lambda: (20).to_bytes(length, order))
for f in (math.perm, math.comb):
    t(f.__name__, lambda: f(2**64 + 1, 2**63))
    t(f.__name__, lambda: f(2**63, 2**63 - 3) > 0 if f is math.comb else 'skip')
    t(f.__name__, lambda: f(2**64, 2**64 + 1))
    t(f.__name__, lambda: f(30, 7))
t("rjust -2**63-1", lambda: "x".rjust(-2**63 - 1))
t("zfill -2**63-1", lambda: b"ab".zfill(-2**63 - 1))
t("center -2**63-1", lambda: "ab".center(-2**63 - 1))
t("split 2**63", lambda: "".split(None, 2**63))
t("rsplit 2**64", lambda: b"".rsplit(None, 2**64))
t("replace count 2**63", lambda: b"".replace(b"\xff", b"\x00\xff", 2**63))
t("replace count 2**63 str", lambda: "".replace("a", "b", 2**63))
t("split empty sep, 2**63", lambda: "".split("", 2**63))
t("expandtabs -2**31-1", lambda: b"\r".expandtabs(-2**31 - 1))
t("expandtabs 2**63-1", lambda: "a\t".expandtabs(2**63 - 1))
t("expandtabs 2**31 kw", lambda: "a\t".expandtabs(tabsize=2**31))
t("expandtabs bytearray 2**31", lambda: bytearray(b"a\t").expandtabs(2**31))
t("expandtabs 3", lambda: ("a\tb".expandtabs(3), b"a\tb".expandtabs(3)))
t("ljust 2**63 str", lambda: "".ljust(2**63))
t("ljust 2**63 bytes", lambda: b"".ljust(2**63))
t("rjust 2**63 bytes", lambda: b"".rjust(2**63))
t("center 2**63 bytearray", lambda: bytearray(b"a").center(2**63))
t("zfill 2**63 str", lambda: "1".zfill(2**63))
t("split bytes 2**63", lambda: b"a b".split(maxsplit=2**63))
t("small", lambda: ("x".rjust(3), b"1".zfill(3), "a b c".split(None, 1), b"a b c".rsplit(None, 1)))
# chr() takes any int: out of range is a ValueError, never an OverflowError.
for v in (2**63, 2**31, -2**31 - 1, -2**63 - 1, 2**64, 0x110000, -1, True):
    t(('chr', v), lambda: chr(v))
