import binascii

data = bytes(range(256)) * 3 + b"tail"
for n in (0, 1, 2, 3, 4, 5, 44, 45):
    chunk = data[:n]
    b = binascii.b2a_base64(chunk)
    print(n, b, binascii.a2b_base64(b) == chunk, binascii.b2a_base64(chunk, newline=False))
    print(binascii.hexlify(chunk), binascii.unhexlify(binascii.hexlify(chunk)) == chunk)
    u = binascii.b2a_uu(chunk)
    print(u, binascii.a2b_uu(u) == chunk, binascii.b2a_uu(chunk, backtick=True))
print(binascii.crc32(b"hello world"), binascii.crc32(b"world", binascii.crc32(b"hello ")), binascii.crc32(b""))
print(binascii.crc_hqx(b"hello world", 0), binascii.crc_hqx(b"d", binascii.crc_hqx(b"hello worl", 0)))
print(binascii.hexlify(b"\x01\x02\x03\x04", "-"), binascii.hexlify(b"\x01\x02\x03\x04\x05", ":", 2),
      binascii.hexlify(b"\x01\x02\x03\x04\x05", ":", -2))
print(binascii.a2b_base64("aGk="), binascii.a2b_base64(b"aG k=\n"), binascii.a2b_base64(b"aGk"[:2] + b"=="))
print(binascii.a2b_qp(b"caf=C3=A9 =\nnext"), binascii.b2a_qp(b"caf\xc3\xa9 = x \n"))
for call, arg in ((binascii.a2b_base64, b"a"), (binascii.a2b_base64, b"aGk"), (binascii.unhexlify, b"abc"),
                  (binascii.unhexlify, b"zz")):
    try:
        call(arg)
    except binascii.Error as e:
        print("Error", e)
try:
    binascii.a2b_base64(b"aGk=x", strict_mode=True)
except binascii.Error as e:
    print("strict", e)
try:
    binascii.crc32("text")
except TypeError as e:
    print(e)
