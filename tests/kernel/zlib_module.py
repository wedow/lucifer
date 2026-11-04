import gzip
import zlib


def show(f, *args, **kwargs):
    try:
        print(repr(f(*args, **kwargs)))
    except BaseException as e:
        print(type(e).__name__, e)


data = b'hello zlib ' * 500 + bytes(range(256)) * 4
print(zlib.ZLIB_VERSION == zlib.ZLIB_RUNTIME_VERSION or True, zlib.MAX_WBITS, zlib.DEFLATED, zlib.DEF_MEM_LEVEL,
      zlib.Z_BEST_COMPRESSION, zlib.Z_FINISH, zlib.Z_SYNC_FLUSH, zlib.DEF_BUF_SIZE, zlib.__version__)

# checksums
print(zlib.crc32(b''), zlib.crc32(b'abc'), zlib.crc32(b'abc', 5), zlib.crc32(data), zlib.crc32(b'x', -1), zlib.crc32(b'x', 2**33 + 7))
print(zlib.adler32(b''), zlib.adler32(b'abc'), zlib.adler32(b'abc', 5), zlib.adler32(data))
print(zlib.crc32(memoryview(b'abc')), zlib.crc32(bytearray(b'abc')))
show(zlib.crc32, 'abc')
show(zlib.adler32, 5)

# one shot
for level in (-1, 0, 1, 6, 9):
    packed = zlib.compress(data, level)
    print(level, len(packed), packed[:2].hex(), zlib.decompress(packed) == data)
print(zlib.compress(b''), zlib.compress(b'a', 1), zlib.compress(b'a', wbits=-15), zlib.compress(b'a', wbits=31)[:4].hex())
print(zlib.decompress(zlib.compress(data, wbits=-15), -15) == data, zlib.decompress(zlib.compress(data, wbits=31), 31) == data,
      zlib.decompress(zlib.compress(data, wbits=31), 47) == data)
show(zlib.compress, data, 10)
show(zlib.compress, data, wbits=100)
show(zlib.decompress, b'not zlib at all')
show(zlib.decompress, zlib.compress(data)[:20])
show(zlib.decompress, b'')
show(zlib.decompress, zlib.compress(data), bufsize=-1)
show(zlib.decompress, 'text')

# streaming
c = zlib.compressobj(6)
pieces = [c.compress(data[i:i + 700]) for i in range(0, len(data), 700)]
pieces.append(c.flush())
stream = b''.join(pieces)
print(len(stream), zlib.decompress(stream) == data)
d = zlib.decompressobj()
out = b''.join(d.decompress(stream[i:i + 100]) for i in range(0, len(stream), 100))
print(len(out), out == data, d.eof, d.unused_data, d.unconsumed_tail)
d = zlib.decompressobj()
print(d.decompress(stream + b'trailing', 1000)[:5], d.eof, d.unused_data, d.unconsumed_tail[:3])
d = zlib.decompressobj()
first = d.decompress(stream, 50)
print(len(first), d.eof, len(d.unconsumed_tail) > 0)
print(len(first + d.decompress(d.unconsumed_tail) + d.flush()) == len(data))
c = zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS, 9, zlib.Z_FILTERED)
raw = c.compress(data) + c.flush(zlib.Z_SYNC_FLUSH) + c.flush()
print(len(raw), zlib.decompress(raw, -15) == data)
c = zlib.compressobj()
c2 = c.copy()
print(c.compress(b'abc') + c.flush() == c2.compress(b'abc') + c2.flush())
show(c.compress, b'x')
show(zlib.compressobj, 10)
show(zlib.compressobj, zlib.Z_DEFAULT_COMPRESSION, 9)
show(zlib.decompressobj().decompress, b'garbage')
show(zlib.decompressobj().decompress, b'x', -1)
show(zlib.decompressobj().flush, 0)

# preset dictionary
zdict = b'hello zlib dictionary text'
c = zlib.compressobj(zdict=zdict)
packed = c.compress(b'hello zlib dictionary text again') + c.flush()
print(packed[:2].hex(), zlib.decompressobj(zdict=zdict).decompress(packed))
show(zlib.decompress, packed)
show(zlib.decompressobj().decompress, packed)

# the incremental decompressor gzip uses
dec = zlib._ZlibDecompressor(wbits=-zlib.MAX_WBITS)
body = zlib.compress(data, wbits=-15)
print(len(dec.decompress(body + b'tail')), dec.eof, dec.unused_data, dec.needs_input)
show(dec.decompress, b'more')
dec = zlib._ZlibDecompressor()
first = dec.decompress(zlib.compress(data), 100)
print(len(first), dec.eof, dec.needs_input)

# gzip over zlib
blob = gzip.compress(data, mtime=0)
print(blob[:4].hex(), len(blob), gzip.decompress(blob) == data)
