b = b'hello world'
print(b, b.upper(), b.find(b'o'), b.split(), len(b), b[0], b[-1], b[1:4])
ba = bytearray(b'abc')
ba.append(100)
ba += b'ef'
print(ba, len(ba), ba[1])
m = memoryview(b'xyz')
print(m.tobytes(), m.tolist(), len(m), m[1:], m.format)
