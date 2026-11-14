"""What a packer generated for the format "<B" would run, against struct.pack. Usage: pack.py CASE WARM REPS."""
import sys
from struct import pack, Struct
NS = [2, 11, 0, 1, 12, 4, 5, 0, 13, 1, 22, 13, 1, 12, 5, 9, 4, 3, 8, 7, 6, 5, 2, 1, 0, 14, 3, 9, 10, 20, 1, 6, 2, 4, 9, 11, 3, 8, 12, 5, 7, 2, 1, 10]
S = Struct("<B")

def gen_B(n):
    # the straight-line body for "<B": the index conversion, the range check, one byte
    if type(n) is not int:
        n = n.__index__()
    if not 0 <= n <= 255:
        raise OverflowError
    return n.to_bytes(1, "little")

def empty():
    for n in NS:
        pass

def pack_b():
    for n in NS:
        pack("<B", n)

def spack():
    p = S.pack
    for n in NS:
        p(n)

def genpack():
    for n in NS:
        gen_B(n)

def tobytes():
    for n in NS:
        n.to_bytes(1, "little")

f = globals()[sys.argv[1]]
warm, reps = int(sys.argv[2]), int(sys.argv[3])
for i in range(warm + reps):
    for _ in range(100):
        f()
