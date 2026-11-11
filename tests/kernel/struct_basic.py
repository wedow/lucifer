import struct

for fmt, args in [('<i', (-5,)), ('>I', (4000000000,)), ('=hHbB', (-2, 65535, -128, 255)), ('<q', (-2**63,)),
                  ('>Q', (2**64 - 1,)), ('<f', (1.5,)), ('>d', (-0.1,)), ('<?', (True,)), ('<3s', (b'abcdef',)),
                  ('<5p', (b'xy',)), ('@i', (1,)), ('<xc', (b'z',)), ('<2h', (1, 2)), ('!H', (513,)),
                  ('<e', (0.5,)), ('@n', (7,)), ('@N', (9,)), ('@P', (1,))]:
    data = struct.pack(fmt, *args)
    print(fmt, data.hex(), struct.unpack(fmt, data), struct.calcsize(fmt))
print(struct.calcsize('@bi'), struct.calcsize('@bq'), struct.calcsize('<bq'), struct.calcsize('@iiq'))
print(struct.unpack_from('<h', b'\0\1\2\3', 2), list(struct.iter_unpack('<h', b'\1\0\2\0')))
buf = bytearray(6)
struct.pack_into('<hh', buf, 1, 7, 8)
print(bytes(buf))
s = struct.Struct('<iH')
print(s.size, s.format, s.pack(1, 2), s.unpack(b'\1\0\0\0\2\0'), repr(s))
for bad in [('<b', 300), ('<B', -1), ('<h', 2**20), ('<i', 'x'), ('<f', 'x'), ('<s', 5)]:
    try:
        struct.pack(*bad)
    except struct.error as e:
        print('error', e)
    except TypeError as e:
        print('TypeError', e)
try:
    struct.unpack('<i', b'\1')
except struct.error as e:
    print(e)
try:
    struct.calcsize('<z')
except struct.error as e:
    print(e)
# A huge repeat count is a run, never expanded: the size check comes first.
for fmt in ('>2147483647q', '<9223372036854775807x', '<4611686018427387904q', '<9223372036854775808b'):
    try:
        print(fmt, struct.calcsize(fmt))
        struct.unpack(fmt, b'\0' * 8)
    except struct.error as e:
        print(fmt, e)
print(struct.pack('<3h2s0i2B', 1, 2, 3, b'xy', 4, 5), struct.unpack('<3h2x2B', b'\1\0\2\0\3\0\0\0\4\5'))
try:
    struct.pack('<3h', 1, 2)
except struct.error as e:
    print(e)
