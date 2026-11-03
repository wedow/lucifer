"""binascii (Modules/binascii.c): conversions between binary data and ASCII
encodings (hex, base64, uuencode, quoted-printable) and the CRCs."""

__all__ = ('Error', 'Incomplete', 'a2b_base64', 'a2b_hex', 'a2b_qp', 'a2b_uu', 'b2a_base64',
           'b2a_hex', 'b2a_qp', 'b2a_uu', 'crc32', 'crc_hqx', 'hexlify', 'unhexlify')


class Error(ValueError):
    pass


class Incomplete(Exception):
    pass


def _ascii(data):
    """Bytes of an ASCII str or any bytes-like object."""
    if isinstance(data, str):
        try:
            return data.encode('ascii')
        except UnicodeEncodeError:
            raise ValueError('string argument should contain only ASCII characters') from None
    try:
        return bytes(memoryview(data))
    except TypeError:
        raise TypeError("argument should be bytes, buffer or ASCII string, not '%s'"
                        % type(data).__name__) from None


def _bytes(data):
    try:
        return bytes(memoryview(data))
    except TypeError:
        raise TypeError("a bytes-like object is required, not '%s'" % type(data).__name__) from None


_CRC32 = []
for _n in range(256):
    _c = _n
    for _ in range(8):
        _c = (_c >> 1) ^ 0xEDB88320 if _c & 1 else _c >> 1
    _CRC32.append(_c)
del _n, _c, _


def crc32(data, crc=0, /):
    """Compute CRC-32 incrementally."""
    table = _CRC32
    c = crc ^ 0xFFFFFFFF if crc >= 0 else (crc & 0xFFFFFFFF) ^ 0xFFFFFFFF
    c &= 0xFFFFFFFF
    for b in _bytes(data):
        c = table[(c ^ b) & 0xFF] ^ (c >> 8)
    return c ^ 0xFFFFFFFF


def crc_hqx(data, crc, /):
    """Compute CRC-CCITT incrementally."""
    crc &= 0xFFFF
    for b in _bytes(data):
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


_HEX = b'0123456789abcdef'


def b2a_hex(data, sep=None, bytes_per_sep=1):
    """Hexadecimal representation of binary data."""
    data = _bytes(data)
    out = bytearray()
    for b in data:
        out.append(_HEX[b >> 4])
        out.append(_HEX[b & 15])
    if sep is None or not data:
        return bytes(out)
    if isinstance(sep, str):
        sep = sep.encode('ascii')
    elif not isinstance(sep, (bytes, bytearray)):
        raise TypeError('sep must be str or bytes.')
    if len(sep) != 1:
        raise ValueError('sep must be length 1.')
    if bytes_per_sep == 0:
        return bytes(out)
    step = abs(bytes_per_sep)
    pairs = [bytes(out[i:i + 2]) for i in range(0, len(out), 2)]
    if bytes_per_sep > 0:
        groups = [pairs[max(0, i - step):i] for i in range(len(pairs), 0, -step)][::-1]
    else:
        groups = [pairs[i:i + step] for i in range(0, len(pairs), step)]
    return bytes(sep).join(b''.join(g) for g in groups)


hexlify = b2a_hex


def a2b_hex(hexstr, /):
    """Binary data of hexadecimal representation."""
    data = _ascii(hexstr)
    if len(data) % 2:
        raise Error('Odd-length string')
    if not all(c in b'0123456789abcdefABCDEF' for c in data):
        raise Error('Non-hexadecimal digit found')
    return bytes(int(data[i:i + 2], 16) for i in range(0, len(data), 2))


unhexlify = a2b_hex

_B64 = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
_B64_INDEX = {c: i for i, c in enumerate(_B64)}


def b2a_base64(data, /, *, newline=True):
    """Base64-code line of data."""
    data = _bytes(data)
    out = bytearray()
    for i in range(0, len(data), 3):
        chunk = data[i:i + 3]
        n = int.from_bytes(chunk + b'\0' * (3 - len(chunk)), 'big')
        quad = [_B64[(n >> s) & 63] for s in (18, 12, 6, 0)]
        if len(chunk) < 3:
            quad[3] = 61
        if len(chunk) < 2:
            quad[2] = 61
        out += bytes(quad)
    if newline:
        out.append(10)
    return bytes(out)


def a2b_base64(data, /, *, strict_mode=False):
    """Decode a line of base64 data."""
    data = _ascii(data)
    if strict_mode and data[:1] == b'=':
        raise Error('Leading padding not allowed')
    out = bytearray()
    left = quad = pads = seen = 0
    padding_started = False
    for pos, c in enumerate(data):
        if c == 61:
            padding_started = True
            if strict_mode and quad == 0:
                raise Error('Excess padding not allowed')
            if quad >= 2:
                pads += 1
                if quad + pads >= 4:
                    if strict_mode and pos + 1 < len(data):
                        raise Error('Excess data after padding')
                    break
            continue
        v = _B64_INDEX.get(c)
        if v is None:
            if strict_mode:
                raise Error('Only base64 data is allowed')
            continue
        if strict_mode and padding_started:
            raise Error('Discontinuous padding not allowed')
        pads = 0
        seen += 1
        if quad == 0:
            left = v
        elif quad == 1:
            out.append((left << 2) | (v >> 4))
            left = v & 15
        elif quad == 2:
            out.append((left << 4) | (v >> 2))
            left = v & 3
        else:
            out.append((left << 6) | v)
        quad = (quad + 1) % 4
    if quad == 1:
        raise Error('Invalid base64-encoded string: number of data characters (%d) cannot be 1 more '
                    'than a multiple of 4' % seen)
    if quad and quad + pads < 4:
        raise Error('Incorrect padding')
    return bytes(out)


def b2a_uu(data, /, *, backtick=False):
    """Uuencode line of data."""
    data = _bytes(data)
    if len(data) > 45:
        raise Error('At most 45 bytes at once')
    def enc(v):
        return (v & 63) + 32 if v & 63 or not backtick else 96
    out = bytearray([32 + len(data)])
    for i in range(0, len(data), 3):
        chunk = data[i:i + 3]
        n = int.from_bytes(chunk + b'\0' * (3 - len(chunk)), 'big')
        out += bytes(enc(n >> s) for s in (18, 12, 6, 0))
    if backtick and out[0] == 32:
        out[0] = 96
    out.append(10)
    return bytes(out)


def a2b_uu(data, /):
    """Decode a line of uuencoded data."""
    data = _ascii(data)
    if not data:
        return b'\0' * 32
    length = (data[0] - 32) & 63
    body = data[1:].rstrip(b'\r\n')
    out = bytearray()
    for i in range(0, len(body), 4):
        quad = body[i:i + 4]
        n = 0
        for j in range(4):
            c = quad[j] if j < len(quad) else 32
            if c < 32 or c > 96:
                raise Error('Illegal char')
            n = (n << 6) | ((c - 32) & 63)
        out += n.to_bytes(3, 'big')
    if len(out) < length:
        out += b'\0' * (length - len(out))
    return bytes(out[:length])


def a2b_qp(data, header=False):
    """Decode a string of qp-encoded data."""
    data = _ascii(data)
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        c = data[i]
        if c == 61:
            if i + 1 >= n:
                break
            if data[i + 1] in b'\r\n':
                i += 2
                if data[i - 1] == 13 and i < n and data[i] == 10:
                    i += 1
                continue
            if len(data[i + 1:i + 3]) == 2 and all(h in b'0123456789abcdefABCDEF' for h in data[i + 1:i + 3]):
                out.append(int(data[i + 1:i + 3], 16))
                i += 3
                continue
            out.append(61)
            i += 1
        elif header and c == 95:
            out.append(32)
            i += 1
        else:
            out.append(c)
            i += 1
    return bytes(out)


def b2a_qp(data, quotetabs=False, istext=True, header=False):
    """Encode a string using quoted-printable encoding."""
    data = _bytes(data)
    out = bytearray()
    linelen = 0
    i, n = 0, len(data)
    while i < n:
        c = data[i]
        if istext and c == 10:
            if out and out[-1] in b' \t':
                last = out.pop()
                out += b'=%02X' % last
            out.append(10)
            linelen = 0
            i += 1
            continue
        quote = (c == 61 or c > 126 or (c < 32 and c not in (9, 10, 13))
                 or (c == 46 and linelen == 0 and (i + 1 == n or data[i + 1] in b'\r\n'))
                 or (c in b' \t' and (quotetabs or i + 1 == n or data[i + 1] in b'\r\n'))
                 or (not istext and c in b'\r\n') or (header and c == 95))
        piece = b'=%02X' % c if quote else (b'_' if header and c == 32 else bytes([c]))
        if linelen + len(piece) > 75 and not (i + 1 == n):
            out += b'=\n'
            linelen = 0
        out += piece
        linelen += len(piece)
        i += 1
    return bytes(out)
