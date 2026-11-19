# a2b_base64 as 3.14 reads padding (RFC 4648 3.3): a pad before the end or
# an excess one is skipped, strict mode names each misplacement; and the
# encoders and the hex codecs over every length, bytes-like and str input.
import binascii

cases = [b'QQ==QQ==', b'aA+=Aa/B', b'=- aa+==AAb', b'=AAa=a!', b'C=+', b'b=\n', b'A=C', b'+=B+', b'QQ=', b'QQ===',
         b'QUJD=', b'QUJD==', b'Q===', b'=QUJD', b'QUI=x', b'QUI=QQ==', b'QQ==\n', b'QU\x80JD', b'QUJ\xffD', b'',
         b'QUJDRA', b'QUJDRA=', b'QUJDRA==', b'QUJDR', b'Q U J D', 'QUJD', bytearray(b'QUJD'), memoryview(b'xQUJDx')[1:5]]
for data in cases:
    for strict in (False, True):
        try:
            print(type(data).__name__, bytes(data, 'ascii') if isinstance(data, str) else bytes(data), strict,
                  binascii.a2b_base64(data, strict_mode=strict))
        except binascii.Error as e:
            print(type(data).__name__, bytes(data, 'ascii') if isinstance(data, str) else bytes(data), strict, 'Error:', e)

data = bytes(range(256)) * 4
for n in list(range(12)) + [255, 256, 1000]:
    chunk = data[n:2 * n]
    for form in (chunk, bytearray(chunk), memoryview(chunk)):
        encoded = binascii.b2a_base64(form)
        assert binascii.a2b_base64(encoded) == chunk, n
        assert binascii.unhexlify(binascii.hexlify(form)) == chunk, n
    print(n, binascii.b2a_base64(chunk, newline=False), binascii.hexlify(chunk)[:24], binascii.b2a_hex(chunk, b':', 3)[-12:])
for bad in (b'0g', b'g0', b'0 ', 'zz', 'aB', b'ABC', b'\xff\xff', 'éa'):
    try:
        print(repr(bad), binascii.unhexlify(bad))
    except (binascii.Error, ValueError) as e:
        print(repr(bad), type(e).__name__, e)
for bad in (1, 'text', [1]):
    for f in (binascii.b2a_base64, binascii.hexlify, binascii.a2b_base64, binascii.unhexlify):
        try:
            f(bad)
        except (TypeError, binascii.Error) as e:
            print(f.__name__, type(e).__name__, e)
